#include "PreCompile.h"
#include "CheckForUpdates.h"
#include "AltTabSettings.h"
#include "Logger.h"
#include "Utils.h"
#include "version.h"
#include "Resource.h"
#include <atomic>
#include <chrono>
#include <filesystem>
#include <format>
#include <fstream>
#include <regex>
#include <shellapi.h>
#include <string>
#include <thread>
#include <wininet.h>

#pragma comment(lib, "wininet.lib")

struct CFU_Info {
    std::string CurrentVersion;
    std::string UpdateVersion;
    std::string Changes;
} g_UpdatesInfo;

namespace {

// Single-flight update-check thread (replaces the old detach() pattern) plus a
// shutdown flag, so at exit StopUpdateCheck() can abort and join it instead of
// the worker crashing into torn-down log4cpp/CRT state.
std::thread         g_updatesThread;
std::atomic<bool>   g_updatesInFlight{false};
std::atomic<bool>   g_updatesStopping{false};

// Currently active WinINet handles; closed by StopUpdateCheck() to unblock a
// worker stuck in a network read (InternetCloseHandle aborts pending I/O).
HINTERNET g_hInternet = nullptr;
HINTERNET g_hConnect  = nullptr;

} // namespace

INT_PTR CALLBACK ATCheckForUpdatesDlgProc(HWND hDlg, UINT message, WPARAM wParam, LPARAM lParam);

// Return 1 if updateVersion > currentVersion, -1 if < , 0 if ==.
int CompareVersions(const std::string& updateVersion, const std::string& currentVersion) {
    const auto uv = Split(updateVersion, ".");
    const auto cv = Split(currentVersion, ".");

    const size_t count = (std::min)(uv.size(), cv.size());
    for (size_t i = 0; i < count; ++i) {
        const int a = std::stoi(uv[i]);
        const int b = std::stoi(cv[i]);
        if (a > b) return 1;
        if (a < b) return -1;
    }

    if (uv.size() > cv.size()) return 1;
    if (uv.size() < cv.size()) return -1;
    return 0;
}

// Function to check for updates from a URL using WinINet.
void CheckForUpdates(const bool quiteMode) {
    AT_LOG_TRACE;

    if (g_updatesStopping.load()) {
        return;
    }

    HINTERNET hInternet = InternetOpen(L"AltTab", INTERNET_OPEN_TYPE_DIRECT, nullptr, nullptr, 0);
    if (!hInternet) {
        AT_LOG_ERROR("Error initializing WinINet");
        if (!quiteMode && !g_updatesStopping.load()) {
            MessageBoxW(
                nullptr,
                L"Failed to initialize network access.",
                AT_PRODUCT_NAMEW,
                MB_OK | MB_ICONERROR | MB_TOPMOST);
        }
        return;
    }
    g_hInternet = hInternet;

    // GitHub's "latest release" API responds with JSON (tag_name + body).
    HINTERNET hConnect = InternetOpenUrl(hInternet, AT_UPDATE_FILE_URL, nullptr, 0, INTERNET_FLAG_RELOAD, 0);
    if (!hConnect) {
        AT_LOG_ERROR("Error opening URL with WinINet");
        if (!quiteMode && !g_updatesStopping.load()) {
            const std::wstring info = std::format(L"Failed to download file [{}], please check again.", AT_UPDATE_FILE_URL);
            MessageBoxW(nullptr, info.c_str(), AT_PRODUCT_NAMEW, MB_OK | MB_ICONERROR | MB_TOPMOST);
        }
        InternetCloseHandle(hInternet);
        g_hInternet = nullptr;
        return;
    }
    g_hConnect = hConnect;

    // Read the response content.
    char buffer[1024] = {};
    DWORD bytesRead = 0;
    std::string contents;
    while (!g_updatesStopping.load() && InternetReadFile(hConnect, buffer, sizeof(buffer), &bytesRead) && bytesRead > 0) {
        contents.append(buffer, bytesRead);
        bytesRead = 0;
    }

    if (g_hConnect) {
        InternetCloseHandle(hConnect);
        g_hConnect = nullptr;
    }
    if (g_hInternet) {
        InternetCloseHandle(hInternet);
        g_hInternet = nullptr;
    }

    if (contents.empty()) {
        AT_LOG_ERROR("Update file is empty.");
        return;
    }

    const std::string currentVersion = AT_VERSION_TEXT;
    const std::string originalContents = contents;

    // GitHub API: tag_name = version, body = changelog.
    std::smatch match;
    std::regex tagRegex(R"tag("tag_name"\s*:\s*"([^"]+)")tag");
    std::regex bodyRegex(R"body("body"\s*:\s*"((?:[^"\\]|\\.)*)")body");

    std::string updateVersion;
    std::string latestChanges;
    if (std::regex_search(originalContents, match, tagRegex)) {
        updateVersion = Trim(match[1].str());
    }
    if (std::regex_search(originalContents, match, bodyRegex)) {
        latestChanges = match[1].str();
        std::string unescaped;
        unescaped.reserve(latestChanges.size());
        for (size_t i = 0; i < latestChanges.size(); ++i) {
            if (latestChanges[i] == '\\' && i + 1 < latestChanges.size()) {
                const char c = latestChanges[++i];
                switch (c) {
                case 'n': unescaped += '\n'; break;
                case 'r': unescaped += '\r'; break;
                case 't': unescaped += '\t'; break;
                default:  unescaped += c;    break;
                }
            } else {
                unescaped += latestChanges[i];
            }
        }
        latestChanges = unescaped;
    }

    AT_LOG_INFO("Current Version: %s", currentVersion.c_str());
    AT_LOG_INFO("Update Version : %s", updateVersion.c_str());

    // Versions look like "N.N.N" (major.minor.patch).
    const auto IsValidVersion = [](const std::string& version) {
        return std::regex_match(version, std::regex(R"(\d+\.\d+\.\d+)"));
    };

    if (updateVersion.empty() || !IsValidVersion(updateVersion)) {
        AT_LOG_ERROR("Invalid update version: %s, please check again!", updateVersion.c_str());
        if (!quiteMode && !g_updatesStopping.load()) {
            const std::string info = std::format("Invalid update version: {}", updateVersion);
            MessageBoxA(nullptr, info.c_str(), AT_PRODUCT_NAMEA, MB_OK | MB_ICONERROR | MB_TOPMOST);
        }
        return;
    }

    if (CompareVersions(updateVersion, currentVersion) > 0) {
        AT_LOG_INFO("Update available!");
        AT_LOG_INFO("Latest Version: %s", updateVersion.c_str());
        g_UpdatesInfo.CurrentVersion = currentVersion;
        g_UpdatesInfo.UpdateVersion  = updateVersion;
        g_UpdatesInfo.Changes        = latestChanges;
        if (!g_updatesStopping.load()) {
            DialogBoxW(g_hInstance, MAKEINTRESOURCE(IDD_CHECK_FOR_UPDATES), nullptr, ATCheckForUpdatesDlgProc);
        }
    } else {
        const std::string info = std::format(
            "You are using the latest version of {}.\n"
            "    - CurrentVersion: {}\n"
            "    - UpdateVersion: {}",
            AT_PRODUCT_NAMEA,
            currentVersion,
            updateVersion);
        if (quiteMode) {
            AT_LOG_INFO(info.c_str());
        } else if (!g_updatesStopping.load()) {
            MessageBoxA(nullptr, info.c_str(), AT_PRODUCT_NAMEA, MB_OK | MB_ICONINFORMATION | MB_TOPMOST);
        }
    }
}

// ----------------------------------------------------------------------------
// Update dialog
// ----------------------------------------------------------------------------

INT_PTR CALLBACK ATCheckForUpdatesDlgProc(HWND hDlg, UINT message, WPARAM wParam, LPARAM lParam) {
    UNREFERENCED_PARAMETER(lParam);
    switch (message) {
    case WM_INITDIALOG: {
        HICON hIcon = LoadIcon(g_hInstance, MAKEINTRESOURCE(IDI_ALTTAB));
        SendMessageW(hDlg, WM_SETICON, ICON_SMALL, (LPARAM)hIcon);
        SendMessageW(hDlg, WM_SETICON, ICON_BIG, (LPARAM)hIcon);

        // Center the dialog.
        const int screenWidth  = GetSystemMetrics(SM_CXSCREEN);
        const int screenHeight = GetSystemMetrics(SM_CYSCREEN);
        RECT dlgRect;
        GetWindowRect(hDlg, &dlgRect);

        const int posX = (screenWidth - (dlgRect.right - dlgRect.left)) / 2;
        const int posY = (screenHeight - (dlgRect.bottom - dlgRect.top)) / 2;
        SetWindowPos(hDlg, HWND_TOP, posX, posY, 0, 0, SWP_NOSIZE);

        SetWindowLong(hDlg, GWL_EXSTYLE, GetWindowLong(hDlg, GWL_EXSTYLE) | WS_EX_APPWINDOW);

        HFONT hFont = CreateFontW(14, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
                                  OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, DEFAULT_QUALITY,
                                  DEFAULT_PITCH | FF_SWISS, L"Lucida Console");
        SendMessageW(GetDlgItem(hDlg, IDC_EDIT_CFU_CHANGES), WM_SETFONT, (WPARAM)hFont, TRUE);
        SetPropW(hDlg, L"CFU_FONT", hFont);

        SetDlgItemTextA(hDlg, IDC_STATIC_CFU_CURRENT_VERSION, g_UpdatesInfo.CurrentVersion.c_str());
        SetDlgItemTextA(hDlg, IDC_STATIC_CFU_UPDATE_VERSION, g_UpdatesInfo.UpdateVersion.c_str());
        SetDlgItemTextA(hDlg, IDC_EDIT_CFU_CHANGES, g_UpdatesInfo.Changes.c_str());
    }
    break;

    case WM_COMMAND:
        if (LOWORD(wParam) == IDOK) {
            EndDialog(hDlg, LOWORD(wParam));
            ShellExecuteW(nullptr, L"open", AT_PRODUCT_LATEST_URL, nullptr, nullptr, SW_SHOWNORMAL);
            return (INT_PTR)TRUE;
        }
        if (LOWORD(wParam) == IDCANCEL) {
            EndDialog(hDlg, LOWORD(wParam));
            return (INT_PTR)TRUE;
        }
        break;

    case WM_DESTROY: {
        HFONT hFont = (HFONT)GetPropW(hDlg, L"CFU_FONT");
        if (hFont) {
            RemovePropW(hDlg, L"CFU_FONT");
            DeleteObject(hFont);
        }
    }
    break;
    }
    return (INT_PTR)FALSE;
}

// ----------------------------------------------------------------------------
// Update timestamp persistence (in %LOCALAPPDATA%\AltTab)
// ----------------------------------------------------------------------------

std::chrono::system_clock::time_point ReadLastCheckForUpdatesTS() {
    const std::filesystem::path filePath = std::filesystem::path(ATLocalAppDataDirPath()) / CHECK_FOR_UPDATES_FILENAME;

    std::ifstream file(filePath.string());
    if (file.is_open()) {
        long long timestamp = 0;
        file >> timestamp;
        return std::chrono::system_clock::from_time_t(timestamp);
    }
    return {};
}

void WriteCheckForUpdatesTS(const std::chrono::system_clock::time_point& timestamp) {
    const std::filesystem::path filePath = std::filesystem::path(ATLocalAppDataDirPath()) / CHECK_FOR_UPDATES_FILENAME;
    std::ofstream file(filePath.string());
    file << std::chrono::system_clock::to_time_t(timestamp);
}

// ----------------------------------------------------------------------------
// Background thread management
// ----------------------------------------------------------------------------

void StartUpdateCheck(bool quiteMode) {
    if (g_updatesStopping.load()) {
        return;
    }
    // One check at a time; a request while a check is already running is a no-op.
    if (g_updatesInFlight.exchange(true)) {
        return;
    }
    // The previous worker (if any) has finished at this point (see the flag
    // above); join its finished handle before reusing the thread object.
    if (g_updatesThread.joinable()) {
        g_updatesThread.join();
    }
    g_updatesThread = std::thread([quiteMode]() {
        CheckForUpdates(quiteMode);
        g_updatesInFlight.store(false);
    });
}

void StopUpdateCheck() {
    g_updatesStopping.store(true);

    // Unblock a worker stuck in a network read; InternetCloseHandle aborts
    // pending WinINet I/O. Closing an already-closed handle just returns FALSE.
    if (HINTERNET c = g_hConnect) {
        InternetCloseHandle(c);
        g_hConnect = nullptr;
    }
    if (HINTERNET i = g_hInternet) {
        InternetCloseHandle(i);
        g_hInternet = nullptr;
    }

    if (g_updatesThread.joinable()) {
        g_updatesThread.join();
    }
    g_updatesInFlight.store(false);
}
#include "PreCompile.h"
#include "CheckForUpdates.h"
#include "AltTabSettings.h"
#include "Logger.h"
#include "Utils.h"
#include "version.h"
#include "Resource.h"
#include <chrono>
#include <filesystem>
#include <format>
#include <fstream>
#include <regex>
#include <shellapi.h>
#include <string>
#include <wininet.h>

#pragma comment(lib, "wininet.lib")

struct CFU_Info {
    std::string CurrentVersion;
    std::string UpdateVersion;
    std::string Changes;
} g_UpdatesInfo;

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

    HINTERNET hInternet = InternetOpen(L"AltTab", INTERNET_OPEN_TYPE_DIRECT, nullptr, nullptr, 0);
    if (!hInternet) {
        AT_LOG_ERROR("Error initializing WinINet");
        if (!quiteMode) {
            MessageBoxW(
                nullptr,
                L"Failed to initialize network access.",
                AT_PRODUCT_NAMEW,
                MB_OK | MB_ICONERROR | MB_TOPMOST);
        }
        return;
    }

    // GitHub's "latest release" API responds with JSON (tag_name + body).
    HINTERNET hConnect = InternetOpenUrl(hInternet, AT_UPDATE_FILE_URL, nullptr, 0, INTERNET_FLAG_RELOAD, 0);
    if (!hConnect) {
        AT_LOG_ERROR("Error opening URL with WinINet");
        if (!quiteMode) {
            const std::wstring info = std::format(L"Failed to download file [{}], please check again.", AT_UPDATE_FILE_URL);
            MessageBoxW(nullptr, info.c_str(), AT_PRODUCT_NAMEW, MB_OK | MB_ICONERROR | MB_TOPMOST);
        }
        InternetCloseHandle(hInternet);
        return;
    }

    // Read the response content.
    char buffer[1024] = {};
    DWORD bytesRead = 0;
    std::string contents;
    while (InternetReadFile(hConnect, buffer, sizeof(buffer), &bytesRead) && bytesRead > 0) {
        contents.append(buffer, bytesRead);
        bytesRead = 0;
    }

    InternetCloseHandle(hConnect);
    InternetCloseHandle(hInternet);

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
        if (!quiteMode) {
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
        DialogBoxW(g_hInstance, MAKEINTRESOURCE(IDD_CHECK_FOR_UPDATES), nullptr, ATCheckForUpdatesDlgProc);
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
        } else {
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
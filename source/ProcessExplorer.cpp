#include "PreCompile.h"
#include "ProcessExplorer.h"
#include "AltTabSettings.h"
#include "Utils.h"
#include "Logger.h"
#include "GlobalData.h"
#include <dwmapi.h>
#include <filesystem>
#include <Psapi.h>

#pragma comment(lib, "dwmapi.lib")

namespace {
// Every request AltTab sends to another window/process is time-limited, so a
// slow-responding or hung window can never stall the keyboard hook or the
// window enumeration (which previously let Windows' own switcher win).
constexpr DWORD kWindowReplyTimeoutMs = 250;
} // namespace

namespace AT {
    void GetPEInfo(
        const std::wstring& filePath,
        std::wstring& description,
        std::wstring& version,
        std::wstring& companyName)
    {
        DWORD dummy;
        DWORD verSize = GetFileVersionInfoSizeW(filePath.c_str(), &dummy);
        if (verSize == 0) {
            return;
        }

        std::vector<BYTE> verData(verSize);
        if (!GetFileVersionInfoW(filePath.c_str(), 0, verSize, verData.data())) {
            return;
        }

        VS_FIXEDFILEINFO* fileInfo = nullptr;
        UINT len = 0;
        if (VerQueryValue(verData.data(), L"\\", (LPVOID*)&fileInfo, &len) && fileInfo) {
            const DWORD major = HIWORD(fileInfo->dwFileVersionMS);
            const DWORD minor = LOWORD(fileInfo->dwFileVersionMS);
            const DWORD build = HIWORD(fileInfo->dwFileVersionLS);
            const DWORD revision = LOWORD(fileInfo->dwFileVersionLS);
            version = std::to_wstring(major) + L"." + std::to_wstring(minor) + L"." + std::to_wstring(build) + L"."
                      + std::to_wstring(revision);
        }

        struct LANGANDCODEPAGE {
            WORD wLanguage;
            WORD wCodePage;
        }* lpTranslate = nullptr;

        UINT cbTranslate = 0;
        if (VerQueryValueW(verData.data(), L"\\VarFileInfo\\Translation", (LPVOID*)&lpTranslate, &cbTranslate)
            && cbTranslate >= sizeof(LANGANDCODEPAGE)) {
            wchar_t subBlock[64] = {};
            LPWSTR lpBuffer = nullptr;
            UINT size = 0;

            swprintf_s(
                subBlock,
                L"\\StringFileInfo\\%04x%04x\\FileDescription",
                lpTranslate[0].wLanguage,
                lpTranslate[0].wCodePage);
            if (VerQueryValueW(verData.data(), subBlock, (LPVOID*)&lpBuffer, &size) && size > 0) {
                description = lpBuffer;
            }

            swprintf_s(
                subBlock,
                L"\\StringFileInfo\\%04x%04x\\CompanyName",
                lpTranslate[0].wLanguage,
                lpTranslate[0].wCodePage);
            if (VerQueryValueW(verData.data(), subBlock, (LPVOID*)&lpBuffer, &size) && size > 0) {
                companyName = lpBuffer;
            }
        }
    }
} // namespace AT

bool IsInvisibleWin10BackgroundAppWindow(HWND hWnd) {
    BOOL isCloaked = FALSE;
    DwmGetWindowAttribute(hWnd, DWMWA_CLOAKED, &isCloaked, sizeof(isCloaked));
    return isCloaked == TRUE;
}

HWND GetOwnerWindowHwnd(HWND hWnd) {
    HWND hOwner = hWnd;
    while (HWND hNext = GetWindow(hOwner, GW_OWNER)) {
        hOwner = hNext;
    }
    return hOwner ? hOwner : hWnd;
}

bool IsDesktopHostWindow(HWND hWnd) {
    // The "desktop" is a real, visible top-level window owned by explorer.exe
    // (Progman / WorkerW). There is no user window behind it to activate, so it
    // must never be listed as an Alt+Tab candidate.
    wchar_t className[64] = {};
    if (GetClassNameW(hWnd, className, _countof(className)) == 0) {
        return false;
    }
    return wcscmp(className, L"Progman") == 0 || wcscmp(className, L"WorkerW") == 0;
}

std::wstring GetProcessNameFromWindow(HWND hWnd) {
    DWORD processId = 0;
    GetWindowThreadProcessId(hWnd, &processId);
    if (processId == 0) {
        return {};
    }

    HANDLE hProcess = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, processId);
    if (hProcess == nullptr) {
        return {};
    }

    wchar_t szProcessPath[MAX_PATH] = {};
    const DWORD size = GetModuleFileNameExW(hProcess, nullptr, szProcessPath, MAX_PATH);
    CloseHandle(hProcess);
    if (size == 0) {
        return {};
    }

    return std::filesystem::path(szProcessPath).filename().wstring();
}

bool IsNonSwitchableShellWindow(HWND hWnd, const std::wstring& title) {
    // UWP/shell composition roots that are not real switchable windows:
    //   - Windows.UI.Core.CoreWindow is the XAML composition surface of a UWP
    //     app; the user-facing window is its ApplicationFrameWindow sibling.
    //   - ApplicationFrameWindow is a window frame. Real frames carry the app
    //     title; an empty-title one is a hidden/unused reuse frame.
    // Only the empty-title instances are proxies (e.g. Settings' CoreWindow,
    // Explorer's hidden frame) so real, titled windows are kept.
    if (!title.empty()) {
        return false;
    }
    wchar_t className[64] = {};
    if (GetClassNameW(hWnd, className, _countof(className)) == 0) {
        return false;
    }
    return wcscmp(className, L"Windows.UI.Core.CoreWindow") == 0
        || wcscmp(className, L"ApplicationFrameWindow") == 0;
}

bool IsAltTabWindow(HWND hWnd) {
    if (!IsWindowVisible(hWnd))
        return false;

    if (IsDesktopHostWindow(hWnd))
        return false;

    HWND hOwner = GetOwnerWindowHwnd(hWnd);
    if (GetLastActivePopup(hOwner) != hWnd)
        return false;

    DWORD windowES = GetWindowLong(hWnd, GWL_EXSTYLE);
    if (windowES & WS_EX_NOACTIVATE) {
        // Cannot be activated (used by proxy/compositor frames of shell apps);
        // selecting such a window would appear to do nothing.
        return false;
    }

    DWORD ownerES = GetWindowLong(hOwner, GWL_EXSTYLE);
    if (ownerES && IsWindowVisible(hOwner) && !((ownerES & WS_EX_TOOLWINDOW) && !(ownerES & WS_EX_APPWINDOW))
        && !IsInvisibleWin10BackgroundAppWindow(hOwner)) {
        return true;
    }

    if (windowES && !((windowES & WS_EX_TOOLWINDOW) && !(windowES & WS_EX_APPWINDOW))
        && !IsInvisibleWin10BackgroundAppWindow(hWnd)) {
        return true;
    }

    return windowES == 0 && ownerES == 0;
}

HICON GetWindowIcon(HWND hWnd) {
    HICON hIcon = nullptr;
    DWORD_PTR result = 0;

    // Time-limited request; a hung window must not block enumeration.
    LRESULT responding = SendMessageTimeout(
        hWnd, WM_GETICON, ICON_BIG, 0, SMTO_ABORTIFHUNG, kWindowReplyTimeoutMs, reinterpret_cast<PDWORD_PTR>(&hIcon));

    if (responding) {
        if (hIcon)
            return hIcon;

        result = 0;
        if (SendMessageTimeout(
                hWnd, WM_GETICON, ICON_SMALL2, 0, SMTO_ABORTIFHUNG, kWindowReplyTimeoutMs, &result)
            && result != 0) {
            return (HICON)result;
        }

        result = 0;
        if (SendMessageTimeout(
                hWnd, WM_GETICON, ICON_SMALL, 0, SMTO_ABORTIFHUNG, kWindowReplyTimeoutMs, &result)
            && result != 0) {
            return (HICON)result;
        }
    }

    hIcon = (HICON)GetClassLongPtr(hWnd, GCLP_HICON);
    if (!hIcon) {
        hIcon = (HICON)GetClassLongPtr(hWnd, GCLP_HICONSM);
    }

    return hIcon ? hIcon : LoadIcon(nullptr, IDI_APPLICATION);
}

namespace {

bool IsExcludedProcess(const std::wstring& processName) {
    if (!g_Settings.ProcessExclusionsEnabled) {
        return false;
    }
    return std::find(
               g_Settings.ProcessExclusionList.begin(),
               g_Settings.ProcessExclusionList.end(),
               processName)
           != g_Settings.ProcessExclusionList.end();
}

BOOL CALLBACK EnumWindowsProc(HWND hWnd, LPARAM lParam) {
    if (!IsAltTabWindow(hWnd)) {
        return TRUE;
    }

    HWND hOwner = GetOwnerWindowHwnd(hWnd);
    DWORD processId = 0;
    GetWindowThreadProcessId(hWnd, &processId);

    if (processId == 0) {
        return TRUE;
    }

    HANDLE hProcess = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, processId);
    if (hProcess == nullptr) {
        return TRUE;
    }

    wchar_t szProcessPath[MAX_PATH] = {};
    if (!GetModuleFileNameEx(hProcess, nullptr, szProcessPath, MAX_PATH)) {
        CloseHandle(hProcess);
        return TRUE;
    }
    CloseHandle(hProcess);

    std::filesystem::path filePath = szProcessPath;

    // Always use the owner window title, otherwise popup title would be
    // displayed. Time-limited request: a slow/hung window must not stall the
    // enumeration (the hotkeys and Alt+Tab handling run on this thread).
    const int bufferSize = 256;
    wchar_t windowTitle[bufferSize] = {};
    DWORD_PTR charsRead = 0;
    SendMessageTimeoutW(
        hOwner,
        WM_GETTEXT,
        bufferSize,
        reinterpret_cast<LPARAM>(windowTitle),
        SMTO_ABORTIFHUNG,
        kWindowReplyTimeoutMs,
        &charsRead);

    std::wstring title = windowTitle;
    if (title.empty()) {
        // The owner chain may lead to a caption-less proxy frame; prefer the
        // window's own text before giving up on a user-facing label.
        wchar_t ownTitle[bufferSize] = {};
        charsRead = 0;
        SendMessageTimeoutW(
            hWnd,
            WM_GETTEXT,
            bufferSize,
            reinterpret_cast<LPARAM>(ownTitle),
            SMTO_ABORTIFHUNG,
            kWindowReplyTimeoutMs,
            &charsRead);
        title = ownTitle;
    }
    if (IsHungAppWindow(hWnd)) {
        title += L" (Not Responding)";
    }

    // Skip shell composition proxies (e.g. Settings' empty CoreWindow or
    // Explorer's hidden ApplicationFrameWindow): there is no user window to
    // bring forward behind them.
    if (IsNonSwitchableShellWindow(hWnd, title)) {
        return TRUE;
    }

    AltTabWindowData item;
    item.hWnd              = hWnd;
    item.hOwner            = hOwner;
    item.hIcon             = GetWindowIcon(hOwner);
    item.Title             = title;
    item.ProcessName       = filePath.filename().wstring();
    item.FullPath          = filePath.wstring();
    item.PID               = processId;
    item.IsConflictProcess = false;
    item.IsBeingClosed     = false;

    AT::GetPEInfo(item.FullPath, item.Description, item.Version, item.CompanyName);

    if (item.Title.empty()) {
        // Shell/compositor frames (some Explorer/Settings proxy windows) don't
        // expose a caption through WM_GETTEXT; show the PE file description or
        // the process name so the row is never blank.
        item.Title = item.Description.empty() ? item.ProcessName : item.Description;
    }

    auto* vItems = reinterpret_cast<std::vector<AltTabWindowData>*>(lParam);
    bool insert = true;
    const bool excluded = IsExcludedProcess(ToLower(item.ProcessName));

    // Alt+Tab / Alt+Ctrl+Tab: all windows.
    if (g_IsAltTab || g_IsAltCtrlTab) {
        insert = !excluded;
    }
    // Alt+Backtick: only windows similar to the one focused when the hotkey
    // was pressed (captured by ShowAltTabWindow before the switcher takes the
    // foreground).
    else if (g_IsAltBacktick) {
        if (g_AltBacktickWndInfo.hWnd != nullptr && !g_AltBacktickWndInfo.ProcessName.empty()) {
            insert = IsSimilarProcess(g_AltBacktickWndInfo.ProcessName, item.ProcessName);
        } else {
            // No usable foreground reference (e.g. the shell desktop is focused);
            // fall back to listing every window rather than an empty list.
            insert = !excluded;
        }
    }

    // Apply the search string, if any.
    if (insert && !g_SearchString.empty()) {
        insert = true;

        const std::vector<std::wstring> searchParts = Split(ToLower(g_SearchString));
        const std::wstring titleLower = ToLower(item.Title);
        const std::wstring processNameLower = ToLower(item.ProcessName);
        const std::wstring descriptionLower = ToLower(item.Description);

        for (const auto& part : searchParts) {
            size_t titleIndex = titleLower.find(part);
            size_t processNameIndex = processNameLower.find(part);
            size_t descriptionIndex = descriptionLower.find(part);

            if (titleIndex != std::wstring::npos) {
                item.TitleHighlights.insert({ titleIndex, titleIndex + part.size() - 1 });
            } else if (g_Settings.FuzzyMatchPercent != 100) {
                const FuzzyMatchResult fuzzyMatchRatio = GetPartialRatioW(part, titleLower);
                if (fuzzyMatchRatio.score >= g_Settings.FuzzyMatchPercent) {
                    item.TitleHighlights.insert({ fuzzyMatchRatio.start_pos, fuzzyMatchRatio.end_pos - 1 });
                    titleIndex = fuzzyMatchRatio.start_pos;
                }
            }

            if (processNameIndex != std::wstring::npos) {
                item.ProcessNameHighlights.insert({ processNameIndex, processNameIndex + part.size() - 1 });
            } else if (g_Settings.FuzzyMatchPercent != 100) {
                const FuzzyMatchResult fuzzyMatchRatio = GetPartialRatioW(part, processNameLower);
                if (fuzzyMatchRatio.score >= g_Settings.FuzzyMatchPercent) {
                    item.ProcessNameHighlights.insert({ fuzzyMatchRatio.start_pos, fuzzyMatchRatio.end_pos - 1 });
                    processNameIndex = fuzzyMatchRatio.start_pos;
                }
            }

            // The file description ("Vi Improved - A Text Editor" for gvim) is
            // not rendered anywhere, so it only needs to match -- there is no
            // highlight to store.
            if (descriptionIndex == std::wstring::npos && !descriptionLower.empty()
                && g_Settings.FuzzyMatchPercent != 100) {
                const FuzzyMatchResult fuzzyMatchRatio = GetPartialRatioW(part, descriptionLower);
                if (fuzzyMatchRatio.score >= g_Settings.FuzzyMatchPercent) {
                    descriptionIndex = fuzzyMatchRatio.start_pos;
                }
            }

            if (titleIndex == std::wstring::npos && processNameIndex == std::wstring::npos
                && descriptionIndex == std::wstring::npos) {
                insert = false;
                break;
            }
        }
    }

    if (insert) {
        vItems->push_back(std::move(item));
    }

    return TRUE;
}

} // namespace

std::vector<AltTabWindowData> GetAltTabWindows() {
    std::vector<AltTabWindowData> altTabWindows;
    EnumWindows(EnumWindowsProc, reinterpret_cast<LPARAM>(&altTabWindows));
    return altTabWindows;
}
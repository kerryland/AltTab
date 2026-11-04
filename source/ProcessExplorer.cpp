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
    // Windows marks windows it does not want switchable (shell input surfaces,
    // minimized UWP background apps, the taskbar/start etc.) as DWM-cloaked.
    // DWMWA_CLOAKED is a flag set: DWM_CLOAKED_APP (1), DWM_CLOAKED_SHELL (2),
    // DWM_CLOAKED_INHERITED (4) -- any non-zero value means not switchable.
    // This is the same attribute the native Alt+Tab / taskbar prune on, and is
    // what keeps e.g. the "Text Input Application" (TextInputHost) out.
    BOOL isCloaked = FALSE;
    DwmGetWindowAttribute(hWnd, DWMWA_CLOAKED, &isCloaked, sizeof(isCloaked));
    return isCloaked != 0;
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

bool IsExcludedProcess(const AltTabEnumContext& context, const std::wstring& processName) {
    if (!context.ProcessExclusionsEnabled) {
        return false;
    }
    return std::find(
               context.ProcessExclusionList.begin(),
               context.ProcessExclusionList.end(),
               processName)
           != context.ProcessExclusionList.end();
}

// Carries the output vector and the immutable state snapshot into the
// enumerator so the enumeration never reads globals and can safely run on a
// worker thread.
struct EnumPack {
    std::vector<AltTabWindowData>* items;
    const AltTabEnumContext*       context;
};

BOOL CALLBACK EnumWindowsProc(HWND hWnd, LPARAM lParam) {
    auto* pack = reinterpret_cast<EnumPack*>(lParam);
    const AltTabEnumContext& ctx = *pack->context;

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
    // enumeration (it runs on a worker thread, never on the hook thread).
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

    auto* vItems = pack->items;
    bool insert = true;
    const bool excluded = IsExcludedProcess(ctx, ToLower(item.ProcessName));

    // Alt+Tab / Alt+Ctrl+Tab: all windows.
    if (ctx.IsAltTab || ctx.IsAltCtrlTab) {
        insert = !excluded;
    }
    // Alt+Backtick: only windows similar to the one focused when the hotkey
    // was pressed (captured by ShowAltTabWindow before the switcher takes the
    // foreground).
    else if (ctx.IsAltBacktick) {
        if (ctx.AltBacktickWndInfo.hWnd != nullptr && !ctx.AltBacktickWndInfo.ProcessName.empty()) {
            insert = IsSimilarProcess(ctx.AltBacktickWndInfo.ProcessName, item.ProcessName);
        } else {
            // No usable foreground reference (e.g. the shell desktop is focused);
            // fall back to listing every window rather than an empty list.
            insert = !excluded;
        }
    }

    // Apply the search string, if any.
    if (insert && !ctx.SearchString.empty()) {
        insert = true;

        const std::vector<std::wstring> searchParts = Split(ToLower(ctx.SearchString));
        const std::wstring titleLower = ToLower(item.Title);
        const std::wstring processNameLower = ToLower(item.ProcessName);
        const std::wstring descriptionLower = ToLower(item.Description);

        for (const auto& part : searchParts) {
            size_t titleIndex = titleLower.find(part);
            size_t processNameIndex = processNameLower.find(part);
            size_t descriptionIndex = descriptionLower.find(part);

            if (titleIndex != std::wstring::npos) {
                item.TitleHighlights.insert({ titleIndex, titleIndex + part.size() - 1 });
            } else if (ctx.FuzzyMatchPercent != 100) {
                const FuzzyMatchResult fuzzyMatchRatio = GetPartialRatioW(part, titleLower);
                if (fuzzyMatchRatio.score >= ctx.FuzzyMatchPercent) {
                    item.TitleHighlights.insert({ fuzzyMatchRatio.start_pos, fuzzyMatchRatio.end_pos - 1 });
                    titleIndex = fuzzyMatchRatio.start_pos;
                }
            }

            if (processNameIndex != std::wstring::npos) {
                item.ProcessNameHighlights.insert({ processNameIndex, processNameIndex + part.size() - 1 });
            } else if (ctx.FuzzyMatchPercent != 100) {
                const FuzzyMatchResult fuzzyMatchRatio = GetPartialRatioW(part, processNameLower);
                if (fuzzyMatchRatio.score >= ctx.FuzzyMatchPercent) {
                    item.ProcessNameHighlights.insert({ fuzzyMatchRatio.start_pos, fuzzyMatchRatio.end_pos - 1 });
                    processNameIndex = fuzzyMatchRatio.start_pos;
                }
            }

            // The file description ("Vi Improved - A Text Editor" for gvim) is
            // not rendered anywhere, so it only needs to match -- there is no
            // highlight to store.
            if (descriptionIndex == std::wstring::npos && !descriptionLower.empty()
                && ctx.FuzzyMatchPercent != 100) {
                const FuzzyMatchResult fuzzyMatchRatio = GetPartialRatioW(part, descriptionLower);
                if (fuzzyMatchRatio.score >= ctx.FuzzyMatchPercent) {
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

namespace {

// ----------------------------------------------------------------------------
// Most-recently-used window tracking (Windows Alt+Tab ordering)
//
// A foreground WinEventHook keeps a most-recently-activated window list. It is
// maintained on the UI thread (the events are delivered to its message loop)
// and snapshotted into AltTabEnumContext, so the enumeration worker only ever
// reads a copy.
// ----------------------------------------------------------------------------
HWINEVENTHOOK       g_activationHook = nullptr;
std::vector<HWND>   g_activationMru;
// Transient foreground events fired by our own switch (destroying the switcher,
// restoring/minimizing, fallback activation tricks) must not reorder the MRU.
ULONGLONG           g_foregroundIgnoredUntil = 0;

void CALLBACK ActivationHookProc(
    HWINEVENTHOOK /*hWinEventHook*/,
    DWORD /*event*/,
    HWND hWnd,
    LONG /*idObject*/,
    LONG /*idChild*/,
    DWORD /*dwEventThread*/,
    DWORD /*dwmsEventTime*/)
{
    // Ignore AltTab's own windows so they never skew the ranking, and skip the
    // transient activations that follow a completed switch (see
    // SuppressForegroundTrackingFor).
    if (hWnd == nullptr || hWnd == g_hMainWnd || hWnd == g_hAltTabWnd) {
        return;
    }
    if (GetTickCount64() < g_foregroundIgnoredUntil) {
        return;
    }
    // Only track real switchable windows (tooltips/IME/transient frames would
    // otherwise pollute the order and occasionally pop to the top of the list).
    if (!IsAltTabWindow(hWnd)) {
        return;
    }

    auto it = std::find(g_activationMru.begin(), g_activationMru.end(), hWnd);
    if (it != g_activationMru.end()) {
        g_activationMru.erase(it);
    }
    g_activationMru.insert(g_activationMru.begin(), hWnd);
    if (g_activationMru.size() > 64) {
        g_activationMru.pop_back();
    }
}

std::vector<HWND> GetActivationMruSnapshot() {
    return g_activationMru;    // UI thread only (same thread that fills it)
}

} // namespace

void InitializeWindowsActivationTracking() {
    if (g_activationHook != nullptr) {
        return;
    }
    g_activationHook = SetWinEventHook(
        EVENT_SYSTEM_FOREGROUND,
        EVENT_SYSTEM_FOREGROUND,
        nullptr,
        &ActivationHookProc,
        0,
        0,
        WINEVENT_OUTOFCONTEXT);
    if (g_activationHook == nullptr) {
        AT_LOG_ERROR("Failed to install foreground tracking hook.");
    }
}

void UninitializeWindowsActivationTracking() {
    if (g_activationHook != nullptr) {
        UnhookWinEvent(g_activationHook);
        g_activationHook = nullptr;
    }
    g_activationMru.clear();
    g_foregroundIgnoredUntil = 0;
}

void MoveWindowToActivationFront(HWND hWnd) {
    if (hWnd == nullptr || hWnd == g_hMainWnd || hWnd == g_hAltTabWnd || !IsAltTabWindow(hWnd)) {
        return;
    }
    auto it = std::find(g_activationMru.begin(), g_activationMru.end(), hWnd);
    if (it != g_activationMru.end()) {
        g_activationMru.erase(it);
    }
    g_activationMru.insert(g_activationMru.begin(), hWnd);
    if (g_activationMru.size() > 64) {
        g_activationMru.pop_back();
    }
}

void SuppressForegroundTrackingFor(DWORD milliseconds) {
    g_foregroundIgnoredUntil = GetTickCount64() + milliseconds;
}

HWND GetPreviousActivationWindow(HWND hCurrent, int direction) {
    if (direction == 0) {
        return nullptr;
    }
    if (hCurrent == nullptr) {
        hCurrent = GetForegroundWindow();
    }

    // The MRU is most-recent-first, so the first switchable window that is not
    // the one we are leaving is exactly the previous window. For a backward
    // switch take the least recently used one instead.
    HWND target = nullptr;
    for (HWND hWnd : g_activationMru) {
        if (hWnd == nullptr || hWnd == hCurrent || hWnd == g_hMainWnd || hWnd == g_hAltTabWnd) {
            continue;
        }
        if (!IsAltTabWindow(hWnd)) {
            continue;
        }
        target = hWnd;
        if (direction > 0) {
            break;
        }
    }

    AT_LOG_INFO(
        "GetPreviousActivationWindow: source=%p dir=%d target=%p",
        (void*)hCurrent,
        direction,
        (void*)target);
    return target;
}

AltTabEnumContext CaptureAltTabEnumContext() {
    AltTabEnumContext context;
    context.IsAltTab                 = g_IsAltTab;
    context.IsAltCtrlTab             = g_IsAltCtrlTab;
    context.IsAltBacktick            = g_IsAltBacktick;
    context.FuzzyMatchPercent        = g_Settings.FuzzyMatchPercent;
    context.ProcessExclusionsEnabled = g_Settings.ProcessExclusionsEnabled;
    context.SearchString             = g_SearchString;
    context.AltBacktickWndInfo       = g_AltBacktickWndInfo;
    context.ProcessExclusionList     = g_Settings.ProcessExclusionList;
    context.ActivationMru            = GetActivationMruSnapshot();
    return context;
}

std::vector<AltTabWindowData> EnumerateAltTabWindows(const AltTabEnumContext& context) {
    std::vector<AltTabWindowData> altTabWindows;
    EnumPack pack{ &altTabWindows, &context };
    EnumWindows(EnumWindowsProc, reinterpret_cast<LPARAM>(&pack));

    // Order the candidates most-recently-activated first, matching Windows'
    // own Alt+Tab: the current (most recent) window is row 0, so the default
    // selection (row 1) is the previously active window and a plain Alt+Tab
    // toggles between the two most recent ones. Untracked windows keep their
    // Z-order at the tail (stable sort).
    if (!context.ActivationMru.empty() && !altTabWindows.empty()) {
        const size_t tail = context.ActivationMru.size();
        auto rank = [&](const AltTabWindowData& window) -> size_t {
            for (size_t i = 0; i < tail; ++i) {
                if (context.ActivationMru[i] == window.hWnd) {
                    return i;
                }
            }
            return tail;
        };
        std::stable_sort(altTabWindows.begin(), altTabWindows.end(), [&](const AltTabWindowData& a, const AltTabWindowData& b) {
            return rank(a) < rank(b);
        });
    }

    return altTabWindows;
}

std::vector<AltTabWindowData> GetAltTabWindows() {
    return EnumerateAltTabWindows(CaptureAltTabEnumContext());
}
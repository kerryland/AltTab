#pragma once

#include <string>
#include <vector>
#include "AltTabWindow.h"

/*!
 * \brief Immutable snapshot of the state the window enumeration depends on.
 *
 * Captured on the UI thread and passed to EnumerateAltTabWindows, which can
 * then run on a worker thread without reading (or racing) any globals. The
 * enumeration talks to every window/process on the desktop and must never run
 * in the low-level keyboard hook (Windows removes a hook that doesn't answer
 * in ~1 s, LowLevelHooksTimeout).
 */
struct AltTabEnumContext {
    bool                            IsAltTab             = false;
    bool                            IsAltCtrlTab         = false;
    bool                            IsAltBacktick        = false;
    int                             FuzzyMatchPercent    = 100;
    bool                            ProcessExclusionsEnabled = false;
    std::wstring                    SearchString;
    AltTabWindowData                AltBacktickWndInfo;
    std::vector<std::wstring>       ProcessExclusionList;
};

/*!
 * \brief Snapshot the current global state the enumeration depends on.
 *        Must be called from the UI thread.
 */
AltTabEnumContext CaptureAltTabEnumContext();

/*!
 * \brief Enumerate all AltTab candidate windows applying the given context
 *        (search string + hotkey mode + exclusions). Thread-safe: does not
 *        read any globals.
 *
 * \return List of AltTab candidate windows.
 */
std::vector<AltTabWindowData> EnumerateAltTabWindows(const AltTabEnumContext& context);

/*!
 * \brief Convenience wrapper (UI thread only): snapshot + enumerate.
 */
std::vector<AltTabWindowData> GetAltTabWindows();

/*!
 * \brief Get the top-most owner window of the given window.
 */
HWND GetOwnerWindowHwnd(HWND hWnd);

/*!
 * \brief Get the most appropriate icon for the given window.
 *
 * The returned icon is owned by the system/process and must not be destroyed.
 */
HICON GetWindowIcon(HWND hWnd);

/*!
 * \brief Whether the given window is a valid AltTab candidate window.
 */
bool IsAltTabWindow(HWND hWnd);

/*!
 * \brief Whether the window is a cloaked (invisible) Win10 background app.
 */
bool IsInvisibleWin10BackgroundAppWindow(HWND hWnd);

/*!
 * \brief Get the process file name (e.g. "brave.exe") that owns the window.
 */
std::wstring GetProcessNameFromWindow(HWND hWnd);

namespace AT {
    /*!
     * \brief Read version info (description, version, company) from a PE file.
     */
    void GetPEInfo(
        const std::wstring& filePath,
        std::wstring& description,
        std::wstring& version,
        std::wstring& companyName);
}
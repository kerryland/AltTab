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
    // Most-recently-activated window order (0 = most recent). Captured on the
    // UI thread so the worker can sort windows into Windows-Alt+Tab order
    // without touching shared state.
    std::vector<HWND>               ActivationMru;
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
 * The result is ordered most-recently-activated first (Windows Alt+Tab style),
 * so the current window is row 0 and a plain Alt+Tab lands on the previous
 * window. Untracked windows keep their Z-order at the end.
 *
 * \return List of AltTab candidate windows.
 */
std::vector<AltTabWindowData> EnumerateAltTabWindows(const AltTabEnumContext& context);

/*!
 * \brief Convenience wrapper (UI thread only): snapshot + enumerate.
 */
std::vector<AltTabWindowData> GetAltTabWindows();

/*!
 * \brief Start tracking foreground-window changes (a WinEventHook) that feeds
 *        the most-recently-used order used by EnumerateAltTabWindows.
 *        Call after the message loop exists (events are delivered to it).
 */
void InitializeWindowsActivationTracking();

/*!
 * \brief Remove the foreground-tracking hook. Call at exit.
 */
void UninitializeWindowsActivationTracking();

/*!
 * \brief Move a window to the front of the most-recently-used list. Used after
 *        AltTab itself activates a window, so the switch is deterministic even
 *        while transient foreground events are being suppressed.
 */
void MoveWindowToActivationFront(HWND hWnd);

/*!
 * \brief Resolve the window a switch would land on using only the activation
 *        MRU (no enumeration). Used as a fallback for a quick Alt+Tab tap when
 *        the asynchronous list has not been built yet.
 *
 * \param hCurrent  The window the switch started from (may be null to use
 *                  the current foreground window).
 * \param direction +1 for forward, -1 for backward.
 * \return The target window, or nullptr if it cannot be determined yet.
 */
HWND GetPreviousActivationWindow(HWND hCurrent, int direction);

/*!
 * \brief Ignore foreground-change events for the given number of ms. AltTab
 *        calls this right after completing a switch, because destroying the
 *        switcher / restoring / minimizing can fire spurious activation events
 *        that would otherwise shuffle the MRU order.
 */
void SuppressForegroundTrackingFor(DWORD milliseconds);

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
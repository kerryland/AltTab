#pragma once

#include <string>
#include <vector>
#include <wtypes.h>
#include <set>

#define CLASS_NAME L"__AltTab_WndCls__"
#define WINDOW_NAME L"AltTab Window"

extern bool g_hAltTabIsBeingClosed; // Is AltTab window being closed
extern bool g_bIgnoreWM_ACTIVATE;   // Ignore WM_ACTIVATE with WA_INACTIVE

/*!
 * @brief Structure to hold AltTab window data
 */
struct AltTabWindowData {
    HWND hWnd;                // Window handle
    HWND hOwner;              // Owner window handle
    HICON hIcon;              // Window icon
    std::wstring Title;       // Window title
    std::wstring ProcessName; // Process name of the window
    std::wstring FullPath;    // Full path of the process
    std::wstring Description; // File description of the process
    std::wstring CompanyName; // Company name of the process
    DWORD PID;                // Process ID
    bool IsConflictProcess;   // Indicates if the process is running from different paths.
    std::wstring Version;     // File version of the process
    bool IsBeingClosed;       // Indicates if the window is being closed

    std::set<std::pair<size_t, size_t>> TitleHighlights;
    std::set<std::pair<size_t, size_t>> ProcessNameHighlights;

    // Compare operator
    bool operator==(const AltTabWindowData& other) const {
        const bool isSame = hWnd == other.hWnd && hOwner == other.hOwner && PID == other.PID && Title == other.Title
                            && ProcessName == other.ProcessName;
        if (!isSame) {
            return false;
        }

        if (TitleHighlights.size() != other.TitleHighlights.size())
            return false;
        for (const auto& highlight : TitleHighlights) {
            if (other.TitleHighlights.find(highlight) == other.TitleHighlights.end()) {
                return false;
            }
        }

        if (ProcessNameHighlights.size() != other.ProcessNameHighlights.size())
            return false;
        for (const auto& highlight : ProcessNameHighlights) {
            if (other.ProcessNameHighlights.find(highlight) == other.ProcessNameHighlights.end()) {
                return false;
            }
        }

        return true;
    }

    bool operator!=(const AltTabWindowData& other) const {
        return !(*this == other);
    }
};

/*!
 * \brief Register the AltTab window class.
 * \return true on success.
 */
bool RegisterAltTabWindow();

/*!
 * \brief Create (and show) the AltTab window.
 */
HWND CreateAltTabWindow();

/*!
 * \brief Show the AltTab window and select the next/previous item.
 *
 * \param hAltTabWnd Reference to the AltTab window handle.
 * \param direction  +1 to move forward, -1 to move backward.
 */
HWND ShowAltTabWindow(HWND& hAltTabWnd, int direction);

/*!
 * \brief Reset the AltTab window state.
 */
void SetAltTabActiveWindow();

/*!
 * \brief Destroy the AltTab window and do the cleanup.
 * \param activate true to activate the currently selected system window.
 */
void DestroyAltTabWindow(const bool activate = false);

void ActivateWindow(HWND hWnd);

BOOL IsHungAppWindowEx(HWND hwnd);

void ShowHelpWindow();
void ShowReadMeWindow();
void ShowReleaseNotesWindow();

std::wstring GetAppDirPath();

void ATShellExecuteEx(const std::wstring& fileName);

void LogLastErrorInfo();

/*!
 * @brief Displays a modal message box (intercepts WM_ACTIVATE so the AltTab
 *        window does not close while the dialog is shown).
 */
int ATMessageBoxW(HWND hWnd, LPCWSTR lpText, LPCWSTR lpCaption, UINT uType);

INT_PTR CALLBACK ATAboutDlgProc(HWND, UINT, WPARAM, LPARAM);
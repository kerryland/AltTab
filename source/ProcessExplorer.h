#pragma once

#include <string>
#include <vector>
#include "AltTabWindow.h"

/*!
 * \brief Enumerate all AltTab candidate windows, applying the current search
 *        string and process exclusions.
 *
 * \return List of AltTab candidate windows.
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
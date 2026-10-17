#pragma once

#include <windows.h>
#include <commctrl.h>

/*!
 * \brief Create the search string edit control and the decorated ListView
 *        inside the AltTab window (called from WM_CREATE).
 *
 * \param hAltTabWnd The AltTab main window.
 */
void InitListView(HWND hAltTabWnd);

/*!
 * \brief Create the icon image list used for the close button.
 *        Must be called once from WinMain.
 */
void InitImageList();

/*!
 * \brief Clear and rebuild the ListView items from the current window list.
 */
void RefreshAltTabWindow();

/*!
 * \brief Release fonts / resources owned by the ListView module (called when
 *        the AltTab window is destroyed).
 */
void AltTabListViewShutdown();

void ATWListViewSelectItem(int rowNumber);
void ATWListViewSelectNextItem();
void ATWListViewSelectPrevItem();
void ATWListViewDeleteItem(int rowNumber);
int  ATWListViewGetSelectedItem();

/*!
 * \brief Translate a virtual key code to a printable character.
 *
 * \return true if uCode maps to a printable character.
 */
bool ATMapVirtualKey(UINT uCode, wchar_t& ch);

/*!
 * \brief Handle WM_NOTIFY (hot-track, tooltip, click) for the AltTab window.
 */
void AltTabWindow_OnNotify(HWND hwnd, NMHDR* pnmhdr);

/*!
 * \brief Close the window at the given list index (asks the app to close).
 */
void ATCloseWindow(int index);

/*!
 * \brief Terminate the process with the given PID.
 * \return true on success.
 */
bool TerminateProcessEx(DWORD pid);

/*!
 * \brief Apply the custom search-string colors (WM_CTLCOLOR* source).
 * \return The background brush handle.
 */
LRESULT AltTabSearchControlColor(HDC hDC, bool isEditControl);

namespace AT {
    /*!
     * \brief Owner-draw a ListView row (DELEGATED from the parent's WM_DRAWITEM).
     */
    BOOL ATListViewDrawItem(HWND hListView, LPDRAWITEMSTRUCT lpDrawItem);
}
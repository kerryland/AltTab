#pragma once

#include <windows.h>

/*!
 * \brief Show the system tray context menu.
 * \return true if the user selected a menu item.
 */
bool ShowTrayContextMenu(HWND hWnd, POINT pt);

/*!
 * \brief Show the AltTab window (list item) context menu.
 */
void ShowContextMenu(HWND hWnd, POINT pt);

/*!
 * \brief Show the item context menu at the center of the selected row.
 */
void ShowContextMenuAtItemCenter();

void ContextMenuItemHandler(HWND hWnd, HMENU hSubMenu, UINT menuItemId);
void TrayContextMenuItemHandler(HWND hWnd, HMENU hSubMenu, UINT menuItemId);

/*!
 * \brief Close any open context menu. Called while destroying the AltTab window.
 */
void DestroyContextMenu();
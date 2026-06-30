#pragma once

#include "resource.h"
#include <string>

// We send this unused value instead of VK_OEM_3 because it doesn't make Windows go "beep"
#define AT_NON_BEEPING_SEARCH_KEY 0x9F

struct ToolTipInfo {
    std::wstring  ToolTipText;
    int           Duration;
};

/*!
 * @brief General settings of the application.
 * When the application starts, it checks the current user privileges.
 */
struct GeneralSettings {
    bool IsProcessElevated; // Is the application running with elevated privileges
    bool IsTaskElevated;    // Is the application set to run elevated (Run with highest privileges in the task options)
    bool IsRunAtStartup;    // Is the application set to run at startup
};

LRESULT CALLBACK LLKeyboardProc(int nCode, WPARAM wp, LPARAM lp);

LRESULT CALLBACK MainWndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam);

/*!
 * @brief Show the AltTab system tray icon context menu.
 * @param hWnd The handle to the window that owns the context menu.
 * @param pt The screen coordinates where the context menu should be displayed.
 * @return true if user selected a menu item, otherwise false.
 */
bool ShowTrayContextMenu(HWND hWnd, POINT pt);

void TrayContextMenuItemHandler(HWND hWnd, HMENU hSubMenu, UINT menuItemId);

void ToggleCheckState(HMENU hMenu, UINT menuItemID);

UINT GetCheckState(HMENU hMenu, UINT menuItemID);

void SetCheckState(HMENU hMenu, UINT menuItemID, UINT fState);

/**
 * Modify the status of RunAtStartup
 * 
 * \param flag  true to make the application run at startup otherwise false.
 * \return Success if the function is successful otherwise false
 */
bool RunAtStartup(const bool runAtStartup, const bool withHighestPrivileges);

bool IsRunAtStartup();

BOOL CALLBACK EnumWindowsProcNAT(HWND hwnd, LPARAM lParam);

bool IsNativeATWDisplayed();

void RestartAltTab();

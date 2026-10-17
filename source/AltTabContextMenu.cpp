#include "PreCompile.h"
#include "AltTabContextMenu.h"
#include "AltTab.h"
#include "AltTabWindow.h"
#include "AltTabListView.h"
#include "AltTabTooltip.h"
#include "AltTabSettings.h"
#include "AutoStart.h"
#include "ProcessExplorer.h"
#include "SettingsDialog.h"
#include "CheckForUpdates.h"
#include "GlobalData.h"
#include "Logger.h"
#include "Utils.h"
#include "version.h"
#include <shlobj.h>
#include <shellapi.h>
#include <thread>

#include "Resource.h"

HMENU g_hContextMenu = nullptr; // Set while a context menu is open

namespace {

void ToggleCheckState(HMENU hMenu, UINT menuItemID) {
    MENUITEMINFO menuItemInfo = { sizeof(MENUITEMINFO) };
    menuItemInfo.fMask = MIIM_STATE;
    GetMenuItemInfo(hMenu, menuItemID, FALSE, &menuItemInfo);
    menuItemInfo.fState ^= MF_CHECKED;
    SetMenuItemInfo(hMenu, menuItemID, FALSE, &menuItemInfo);
}

UINT GetCheckState(HMENU hMenu, UINT menuItemID) {
    MENUITEMINFO menuItemInfo = { sizeof(MENUITEMINFO) };
    menuItemInfo.fMask = MIIM_STATE;
    GetMenuItemInfo(hMenu, menuItemID, FALSE, &menuItemInfo);
    return menuItemInfo.fState & MF_CHECKED;
}

void SetCheckState(HMENU hMenu, UINT menuItemID, UINT fState) {
    MENUITEMINFO menuItemInfo = { sizeof(MENUITEMINFO) };
    menuItemInfo.fMask  = MIIM_STATE;
    menuItemInfo.fState = fState;
    SetMenuItemInfo(hMenu, menuItemID, FALSE, &menuItemInfo);
}

void CopyToClipboard(const std::wstring& text) {
    if (!OpenClipboard(g_hAltTabWnd)) {
        AT_LOG_ERROR("OpenClipboard failed");
        return;
    }

    EmptyClipboard();
    HGLOBAL hGlobal = GlobalAlloc(GMEM_MOVEABLE, (text.size() + 1) * sizeof(wchar_t));
    if (hGlobal) {
        wchar_t* pGlobal = (wchar_t*)GlobalLock(hGlobal);
        if (pGlobal) {
            wcscpy_s(pGlobal, text.size() + 1, text.c_str());
            GlobalUnlock(hGlobal);
            SetClipboardData(CF_UNICODETEXT, hGlobal);
        } else {
            GlobalFree(hGlobal);
        }
    }
    CloseClipboard();
}

void BrowseToFile(const std::wstring& filepath) {
    if (filepath.empty())
        return;

    LPITEMIDLIST pidl = ILCreateFromPathW(filepath.c_str());
    if (pidl) {
        SHOpenFolderAndSelectItems(pidl, 0, nullptr, 0);
        ILFree(pidl);
    }
}

} // namespace

void DestroyContextMenu() {
    if (g_hContextMenu) {
        AT_LOG_INFO("g_hContextMenu: %p", g_hContextMenu);

        HWND hActiveWnd = GetForegroundWindow();
        AT_LOG_INFO("Sending WM_CLOSE to context menu window: %#010x", (UINT_PTR)hActiveWnd);
        PostMessageW(hActiveWnd, WM_KEYDOWN, VK_ESCAPE, 0);

        DestroyMenu(g_hContextMenu);
        g_hContextMenu = nullptr;
    }
}

// ----------------------------------------------------------------------------
// AltTab window (list item) context menu
// ----------------------------------------------------------------------------

void ContextMenuItemHandler(HWND hWnd, HMENU /*hSubMenu*/, UINT menuItemId) {
    switch (menuItemId) {
    case ID_CONTEXTMENU_CLOSE_WINDOW: {
        const int ind = ATWListViewGetSelectedItem();
        if (ind != -1) {
            ATCloseWindow(ind);
        }
    }
    break;

    case ID_CONTEXTMENU_KILL_PROCESS: {
        const int ind = ATWListViewGetSelectedItem();
        if (ind != -1) {
            TerminateProcessEx(g_AltTabWindows[ind].PID);
        }
    }
    break;

    case ID_CONTEXTMENU_CLOSEALLWINDOWS: {
        AT_LOG_INFO("ID_CONTEXTMENU_CLOSEALLWINDOWS");

        // This MessageBox would also close the AltTab window, so disable the
        // window-count timer and ignore WM_ACTIVATE while it is shown.
        KillTimer(hWnd, TIMER_WINDOW_COUNT);
        g_bIgnoreWM_ACTIVATE = true;

        bool closeAllWindows = true;
        if (g_Settings.PromptTerminateAll) {
            const int result = MessageBoxW(
                hWnd,
                L"Are you sure you want to close all windows?",
                AT_PRODUCT_NAMEW L": Close All Windows",
                MB_YESNO | MB_ICONQUESTION | MB_DEFBUTTON2);
            closeAllWindows = (result == IDYES);
        }

        if (closeAllWindows) {
            for (size_t i = 0; i < g_AltTabWindows.size(); ++i) {
                ATCloseWindow((int)i);
            }
        } else {
            SetAltTabActiveWindow();
        }

        g_bIgnoreWM_ACTIVATE = false;
        SetTimer(hWnd, TIMER_WINDOW_COUNT, TIMER_WINDOW_COUNT_ELAPSE, nullptr);
    }
    break;

    case ID_CONTEXTMENU_KILLALLPROCESSES: {
        AT_LOG_INFO("ID_CONTEXTMENU_KILLALLPROCESSES");

        KillTimer(hWnd, TIMER_WINDOW_COUNT);

        bool terminateAllWindows = true;
        if (g_Settings.PromptTerminateAll) {
            const int result = ATMessageBoxW(
                hWnd,
                L"Are you sure you want to terminate all windows?",
                AT_PRODUCT_NAMEW L": Terminate All Windows",
                MB_YESNO | MB_ICONQUESTION | MB_DEFBUTTON2);
            terminateAllWindows = (result == IDYES);
        }

        if (terminateAllWindows) {
            for (auto& windowData : g_AltTabWindows) {
                TerminateProcessEx(windowData.PID);
            }
        } else {
            SetAltTabActiveWindow();
        }

        SetTimer(hWnd, TIMER_WINDOW_COUNT, TIMER_WINDOW_COUNT_ELAPSE, nullptr);
    }
    break;

    case ID_CONTEXTMENU_OPEN_PATH: {
        const int ind = ATWListViewGetSelectedItem();
        if (ind != -1 && ind < (int)g_AltTabWindows.size()) {
            const std::wstring filepath = g_AltTabWindows[ind].FullPath;
            DestroyAltTabWindow();
            BrowseToFile(filepath);
        }
    }
    break;

    case ID_CONTEXTMENU_COPY_PATH: {
        const int ind = ATWListViewGetSelectedItem();
        if (ind != -1 && ind < (int)g_AltTabWindows.size()) {
            const std::wstring filepath = g_AltTabWindows[ind].FullPath;
            DestroyAltTabWindow();
            CopyToClipboard(filepath);
        }
    }
    break;

    case ID_CONTEXTMENU_COPY_TITLE: {
        const int ind = ATWListViewGetSelectedItem();
        if (ind != -1 && ind < (int)g_AltTabWindows.size()) {
            const std::wstring title = g_AltTabWindows[ind].Title;
            DestroyAltTabWindow();
            CopyToClipboard(title);
        }
    }
    break;

    case ID_CONTEXTMENU_ABOUTALTTAB:
    case ID_TRAYCONTEXTMENU_ABOUTALTTAB: {
        AT_LOG_INFO("ID_CONTEXTMENU_ABOUTALTTAB");
        DestroyAltTabWindow();
        DialogBoxW(g_hInstance, MAKEINTRESOURCE(IDD_ABOUTBOX), g_hMainWnd, ATAboutDlgProc);
    }
    break;

    case ID_CONTEXTMENU_SETTINGS: {
        AT_LOG_INFO("ID_CONTEXTMENU_SETTINGS");
        DestroyAltTabWindow();
        if (g_hSettingsWnd == nullptr) {
            DialogBoxW(g_hInstance, MAKEINTRESOURCE(IDD_SETTINGS), g_hMainWnd, ATSettingsDlgProc);
        } else {
            SetForegroundWindow(g_hSettingsWnd);
        }
    }
    break;

    case ID_TRAYCONTEXTMENU_README:
        ShowReadMeWindow();
        break;

    case ID_TRAYCONTEXTMENU_HELP:
        ShowHelpWindow();
        break;

    case ID_TRAYCONTEXTMENU_RELEASENOTES:
        ShowReleaseNotesWindow();
        break;

    case ID_TRAYCONTEXTMENU_CHECKFORUPDATES: {
        DestroyAltTabWindow();
        HideCustomToolTip();
        ShowCustomToolTip(L"Checking for updates..., please wait.");

        // Run in a thread so the tooltip can display.
        std::thread thr(CheckForUpdates, false);
        thr.detach();
    }
    break;

    case ID_TRAYCONTEXTMENU_RELOADALTTABSETTINGS: {
        ATLoadSettings();
        ShowCustomToolTip(L"Settings reloaded successfully.", 3000);
    }
    break;

    case ID_TRAYCONTEXTMENU_RESTART:
        AT_LOG_INFO("ID_TRAYCONTEXTMENU_RESTART");
        DestroyAltTabWindow();
        RestartApplication();
        break;

    case ID_CONTEXTMENU_EXIT:
    case ID_TRAYCONTEXTMENU_EXIT:
        AT_LOG_INFO("ID_CONTEXTMENU_EXIT");
        DestroyAltTabWindow();
        PostQuitMessage(0);
        break;
    }
}

void ShowContextMenuAtItemCenter() {
    RECT itemRect;
    ListView_GetItemRect(g_hListView, g_SelectedIndex, &itemRect, LVIR_BOUNDS);

    POINT center;
    center.x = (itemRect.left + itemRect.right) / 2;
    center.y = (itemRect.top + itemRect.bottom) / 2;

    ClientToScreen(g_hListView, &center);

    ShowContextMenu(g_hAltTabWnd, center);
}

void ShowContextMenu(HWND hWnd, POINT pt) {
    HMENU hMenu = LoadMenu(g_hInstance, MAKEINTRESOURCE(IDR_CONTEXTMENU));
    if (!hMenu) {
        return;
    }

    HMENU hSubMenu = GetSubMenu(hMenu, 0);
    if (hSubMenu) {
        UINT uFlags = TPM_RIGHTBUTTON;
        uFlags |= (GetSystemMetrics(SM_MENUDROPALIGNMENT) != 0) ? TPM_RIGHTALIGN : TPM_LEFTALIGN;

        g_hContextMenu = hSubMenu;

        uFlags |= TPM_RETURNCMD;
        const UINT menuItemId = TrackPopupMenuEx(hSubMenu, uFlags, pt.x, pt.y, hWnd, nullptr);

        if (menuItemId != 0) {
            g_hContextMenu = nullptr;
            ContextMenuItemHandler(hWnd, hSubMenu, menuItemId);
        }
    }

    DestroyMenu(hMenu);
}

// ----------------------------------------------------------------------------
// System tray context menu
// ----------------------------------------------------------------------------

void TrayContextMenuItemHandler(HWND /*hWnd*/, HMENU hSubMenu, UINT menuItemId) {
    switch (menuItemId) {
    case ID_TRAYCONTEXTMENU_ABOUTALTTAB:
        AT_LOG_INFO("ID_TRAYCONTEXTMENU_ABOUTALTTAB");
        DialogBox(g_hInstance, MAKEINTRESOURCE(IDD_ABOUTBOX), g_hMainWnd, ATAboutDlgProc);
        break;

    case ID_TRAYCONTEXTMENU_README:
        ShowReadMeWindow();
        break;

    case ID_TRAYCONTEXTMENU_HELP:
        ShowHelpWindow();
        break;

    case ID_TRAYCONTEXTMENU_RELEASENOTES:
        ShowReleaseNotesWindow();
        break;

    case ID_TRAYCONTEXTMENU_SETTINGS:
        AT_LOG_INFO("ID_TRAYCONTEXTMENU_SETTINGS");
        if (g_hSettingsWnd == nullptr) {
            DialogBoxW(g_hInstance, MAKEINTRESOURCE(IDD_SETTINGS), g_hMainWnd, ATSettingsDlgProc);
        } else {
            SetForegroundWindow(g_hSettingsWnd);
        }
        break;

    case ID_TRAYCONTEXTMENU_DISABLEALTTAB: {
        AT_LOG_INFO("ID_TRAYCONTEXTMENU_DISABLEALTTAB");
        const bool disableAltTab = !(GetCheckState(hSubMenu, menuItemId) == MF_CHECKED);
        g_Settings.DisableAltTab = disableAltTab;

        if (disableAltTab) {
            UnhookWindowsHookEx(g_KeyboardHook);
            g_KeyboardHook = nullptr;
        } else {
            g_KeyboardHook = SetWindowsHookEx(WH_KEYBOARD_LL, LLKeyboardProc, g_hInstance, 0);
        }
    }
    break;

    case ID_TRAYCONTEXTMENU_CHECKFORUPDATES: {
        AT_LOG_INFO("ID_TRAYCONTEXTMENU_CHECKFORUPDATES");
        ShowCustomToolTip(L"Checking for updates..., please wait.");

        std::thread thr(CheckForUpdates, false);
        thr.detach();
    }
    break;

    case ID_TRAYCONTEXTMENU_RUNATSTARTUP: {
        AT_LOG_INFO("ID_TRAYCONTEXTMENU_RUNATSTARTUP");
        const bool checked = (GetCheckState(hSubMenu, menuItemId) == MF_CHECKED);
        RunAtStartup(!checked, g_GeneralSettings.IsProcessElevated);
        ToggleCheckState(hSubMenu, menuItemId);
    }
    break;

    case ID_TRAYCONTEXTMENU_RUNASADMIN: {
        AT_LOG_INFO("ID_TRAYCONTEXTMENU_RUNASADMIN");
        const bool checked = (GetCheckState(hSubMenu, menuItemId) == MF_CHECKED);
        const bool isRunAtStartup = IsRunAtStartup();

        if (!checked) {
            // Enable "Run as Administrator".
            if (isRunAtStartup) {
                if (g_GeneralSettings.IsProcessElevated) {
                    // Already elevated: just switch the task's run level.
                    DeleteAutoStartTask();
                    CreateAutoStartTask(true);
                    return;
                }
                const int result = MessageBoxW(
                    nullptr,
                    L"Do you want to always run AltTab as Administrator since 'Run at Startup' is enabled?\n\n"
                    L"This will change the AltTab startup task to 'Run with highest privileges' option in Task Scheduler.\n"
                    L"Going to relaunch AltTab with admin privileges...",
                    AT_PRODUCT_NAMEW,
                    MB_YESNO | MB_ICONQUESTION | MB_DEFBUTTON1);
                if (result == IDYES) {
                    RelaunchAsAdminAndExit(true, true);
                    return;
                }
            } else {
                RelaunchAsAdminAndExit(true, false);
                return;
            }
        } else {
            // Disable "Run as Administrator".
            if (isRunAtStartup) {
                DeleteAutoStartTask();
                CreateAutoStartTask(false);
            } else {
                RelaunchAsAdminAndExit(false, false);
                return;
            }
        }
        ToggleCheckState(hSubMenu, menuItemId);
    }
    break;

    case ID_TRAYCONTEXTMENU_CLOSEALLWINDOWS: {
        std::vector<AltTabWindowData> altTabWindows = GetAltTabWindows();
        AT_LOG_INFO("ID_TRAYCONTEXTMENU_CLOSEALLWINDOWS: altTabWindows.size(): %zu", altTabWindows.size());
        if (altTabWindows.empty()) {
            ShowCustomToolTip(L"No windows to close.", 3000);
            return;
        }

        const int result = ATMessageBoxW(
            g_hMainWnd,
            L"Are you sure you want to close all windows?",
            AT_PRODUCT_NAMEW L": Close All Windows",
            MB_YESNO | MB_ICONQUESTION | MB_DEFBUTTON2);

        if (result == IDYES) {
            for (const auto& windowData : altTabWindows) {
#ifdef _DEBUG
                // Ignore the developer environment processes in debug builds.
                static const wchar_t* const IGNORED_IN_DEBUG[] = {
                    L"devenv.exe", L"Code.exe", L"Outlook.exe", L"xplorer2_64.exe", L"msedge.exe", L"OneNote.exe",
                    L"Fork.exe", L"ConEmu64.exe", L"ms-teams.exe", L"AltTab.exe"
                };
                bool ignore = false;
                for (const wchar_t* ignored : IGNORED_IN_DEBUG) {
                    if (EqualsIgnoreCase(windowData.ProcessName, ignored)) {
                        ignore = true;
                        break;
                    }
                }
                if (ignore) {
                    continue;
                }
#endif // _DEBUG
                PostMessage(windowData.hWnd, WM_CLOSE, 0, 0);
            }
        }
    }
    break;

    case ID_TRAYCONTEXTMENU_RELOADALTTABSETTINGS: {
        AT_LOG_INFO("ID_TRAYCONTEXTMENU_RELOADALTTABSETTINGS");
        ATLoadSettings();
        ShowCustomToolTip(L"Settings reloaded successfully.", 3000);
    }
    break;

    case ID_TRAYCONTEXTMENU_RESTART: {
        AT_LOG_INFO("ID_TRAYCONTEXTMENU_RESTART");
        RestartAltTab();
    }
    break;

    case ID_TRAYCONTEXTMENU_EXIT:
        AT_LOG_INFO("ID_TRAYCONTEXTMENU_EXIT");
        PostQuitMessage(0);
        break;
    }
}

bool ShowTrayContextMenu(HWND hWnd, POINT pt) {
    // Refresh the cached general settings (elevation / run-at-startup may have
    // changed while we were running).
    g_GeneralSettings = GetGeneralSettings();
    AT_LOG_INFO(
        "GeneralSettings: IsProcessElevated = %d, IsTaskElevated = %d, IsRunAtStartup = %d",
        g_GeneralSettings.IsProcessElevated,
        g_GeneralSettings.IsTaskElevated,
        g_GeneralSettings.IsRunAtStartup);

    HMENU hMenu = LoadMenu(g_hInstance, MAKEINTRESOURCE(IDC_TRAY_CONTEXTMENU));
    UINT menuItemId = 0;
    if (!hMenu) {
        return false;
    }

    HMENU hSubMenu = GetSubMenu(hMenu, 0);
    if (hSubMenu) {
        // The window must be foreground before TrackPopupMenu so the menu
        // disappears when the user clicks elsewhere.
        SetForegroundWindow(hWnd);

        const bool isRunAtStartup = IsRunAtStartup();
        if (isRunAtStartup) {
            SetCheckState(hSubMenu, ID_TRAYCONTEXTMENU_RUNATSTARTUP, MF_CHECKED);
        }

        // Rename the "Run as Administrator" item when run-at-startup is on.
        MENUITEMINFOW mii = { sizeof(mii) };
        mii.fMask     = MIIM_STRING;
        mii.dwTypeData = (LPWSTR)(isRunAtStartup ? L"Always run as Administrator" : L"Run as Administrator");
        SetMenuItemInfoW(hMenu, ID_TRAYCONTEXTMENU_RUNASADMIN, FALSE, &mii);

        if (g_GeneralSettings.IsProcessElevated) {
            if (isRunAtStartup) {
                SetCheckState(
                    hSubMenu,
                    ID_TRAYCONTEXTMENU_RUNASADMIN,
                    IsTaskRunWithHighestPrivileges() ? MF_CHECKED : MF_UNCHECKED);
            } else {
                SetCheckState(hSubMenu, ID_TRAYCONTEXTMENU_RUNASADMIN, MF_CHECKED);
            }

            // A non-elevated process can't be spawned from an elevated one.
            if (!isRunAtStartup) {
                EnableMenuItem(hSubMenu, ID_TRAYCONTEXTMENU_RUNASADMIN, MF_BYCOMMAND | MF_DISABLED | MF_GRAYED);
            } else {
                EnableMenuItem(hSubMenu, ID_TRAYCONTEXTMENU_RUNASADMIN, MF_BYCOMMAND | MF_ENABLED);
            }
        }

        if (g_Settings.DisableAltTab) {
            SetCheckState(hSubMenu, ID_TRAYCONTEXTMENU_DISABLEALTTAB, MF_CHECKED);
        }

        UINT uFlags = TPM_RIGHTBUTTON;
        uFlags |= (GetSystemMetrics(SM_MENUDROPALIGNMENT) != 0) ? TPM_RIGHTALIGN : TPM_LEFTALIGN;

        g_hContextMenu = hSubMenu;

        // TPM_RETURNCMD: the selected item id is the return value.
        uFlags |= TPM_RETURNCMD;
        menuItemId = TrackPopupMenuEx(hSubMenu, uFlags, pt.x, pt.y, hWnd, nullptr);
        if (menuItemId != 0) {
            g_hContextMenu = nullptr;
            TrayContextMenuItemHandler(hWnd, hSubMenu, menuItemId);
        }
    }

    DestroyMenu(hMenu);
    return menuItemId != 0;
}
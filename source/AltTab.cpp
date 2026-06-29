// AltTab.cpp : Defines the entry point for the application.
//
#include "PreCompile.h"

#include "AltTab.h"
#include "Logger.h"
#include "Utils.h"
#include "AltTabWindow.h"
#include <string>
#include <format>
#include "AltTabSettings.h"
#include <shellapi.h>
#include "Resource.h"
#include "version.h"
#include <CommCtrl.h>
#include "GlobalData.h"
#include <filesystem>
#include "CheckForUpdates.h"
#include <thread>

#pragma comment(lib, "comctl32.lib")
#pragma comment(lib, "taskschd.lib")
#pragma comment(lib, "comsuppw.lib")
#pragma comment(lib, "gdiplus.lib")

#pragma comment(                                         \
        linker,                                          \
            "/manifestdependency:\"type='win32' "        \
            "name='Microsoft.Windows.Common-Controls' "  \
            "version='6.0.0.0' "                         \
            "processorArchitecture='*' "                 \
            "publicKeyToken='6595b64144ccf1df' "         \
            "language='*' "                              \
            "\"")

// ----------------------------------------------------------------------------
// Global Variables:
// ----------------------------------------------------------------------------
HINSTANCE       g_hInstance;                                   // Current instance
HHOOK           g_KeyboardHook;                                // Keyboard Hook
HWND            g_hAltTabWnd           = nullptr;              // AltTab window handle
HWND            g_hFGWnd               = nullptr;              // Foreground window handle
HWND            g_hMainWnd             = nullptr;              // AltTab main window handle
HWND            g_hSettingsWnd         = nullptr;              // AltTab settings window handle
UINT_PTR        g_TooltipTimerId;
bool            g_TooltipVisible       = false;                // Is tooltip visible or not
TOOLINFO        g_ToolInfo             = {};                   // Custom tool tip
bool            g_IsAltKeyPressed      = false;                // Is Alt key pressed
DWORD           g_LastAltKeyPressTime  = 0;                    // Last Alt key press time
bool            g_IsAltTab             = false;                // Is Alt+Tab pressed
bool            g_IsAltCtrlTab         = false;                // Is Alt+Ctrl+Tab pressed
bool            g_IsAltBacktick        = false;                // Is Alt+Backtick pressed
DWORD           g_MainThreadID         = GetCurrentThreadId(); // Main thread ID
DWORD           g_idThreadAttachTo     = 0;
HIMAGELIST      g_hImageList           = nullptr;
HIMAGELIST      g_hLVImageList         = nullptr;
int             g_nImgCloseActiveInd   = -1;
int             g_nImgCloseInactiveInd = -1;

GeneralSettings g_GeneralSettings; // General settings


IsHungAppWindowFunc g_pfnIsHungAppWindow = nullptr;

UINT const WM_USER_ALTTAB_TRAYICON = WM_APP + 1;

HWND CreateMainWindow(HINSTANCE hInstance);
BOOL AddNotificationIcon(HWND hWndTrayIcon);
void CALLBACK CheckAltKeyIsReleased(HWND hWnd, UINT uMsg, UINT_PTR idEvent, DWORD dwTime);
void CALLBACK CheckForUpdatesTimerCB(HWND hWnd, UINT uMsg, UINT_PTR idEvent, DWORD dwTime);
int  GetCurrentYear();

namespace {
    GeneralSettings GetGeneralSettings() {
        GeneralSettings settings;
        settings.IsProcessElevated = IsProcessElevated();
        settings.IsTaskElevated = IsTaskRunWithHighestPrivileges();
        settings.IsRunAtStartup = IsRunAtStartup();
        return settings;
    }

    void RemoveStartupFromRegistry() {
        HKEY hKey;
        LONG result = RegOpenKeyEx(
            HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Run", 0, KEY_SET_VALUE, &hKey);

        if (result == ERROR_SUCCESS) {
            result = RegDeleteValue(hKey, AT_PRODUCT_NAMEW);
        }
        RegCloseKey(hKey);
    }
}

// ----------------------------------------------------------------------------
// Main
// ----------------------------------------------------------------------------
int APIENTRY wWinMain(
    _In_        HINSTANCE   hInstance,
    _In_opt_    HINSTANCE   hPrevInstance,
    _In_        LPWSTR      lpCmdLine,
    _In_        int         /*nCmdShow*/)
{
    UNREFERENCED_PARAMETER(hPrevInstance);
    UNREFERENCED_PARAMETER(lpCmdLine);

    g_hInstance = hInstance; // Store instance handle in our global variable

    // Set process DPI awareness
    if (!SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE)) {
        // Handle error if setting DPI awareness fails
        // For simplicity, you may just display a message box
        AT_LOG_ERROR("Failed to set DPI awareness!");
    }

#if 0
    // Make sure only one instance is running
    HANDLE hMutex = CreateMutex(nullptr, TRUE, AT_PRODUCT_NAMEW);
    if (GetLastError() == ERROR_ALREADY_EXISTS) {
        // Another instance is running, handle it here
        std::wstring info = std::format(L"Another instance of {} is running!", AT_PRODUCT_NAMEW);
        MessageBoxW(nullptr, info.c_str(), AT_PRODUCT_NAMEW, MB_OK | MB_ICONEXCLAMATION);
        if (hMutex != nullptr) {
            CloseHandle(hMutex);
        }
        return 1;
    }
#endif // 0

#ifdef _AT_LOGGER
    CreateLogger();
    AT_LOG_INFO("-------------------------------------------------------------------------------");
    AT_LOG_INFO("CreateLogger done.");
#endif // _AT_LOGGER

    // Log application module path
    const std::wstring applicationPath = GetApplicationPath();
    AT_LOG_INFO("Application Info");
    AT_LOG_INFO("  - Path      : %ls", applicationPath.c_str());
    AT_LOG_INFO("  - Version   : %s", AT_FULL_VERSIONA);
    AT_LOG_INFO("  - ProcessID : %d", GetCurrentProcessId());

    // Initialize the common things like common controls, GDI+ etc.
    InitGDIPlus();
    InitializeCOM();
    InitImageList();

    // Load GeneralSettings
    g_GeneralSettings = GetGeneralSettings();
    AT_LOG_INFO(
        "GeneralSettings: IsProcessElevated = %d, IsTaskElevated = %d, IsRunAtStartup = %d",
        g_GeneralSettings.IsProcessElevated,
        g_GeneralSettings.IsTaskElevated,
        g_GeneralSettings.IsRunAtStartup);

    CreateCustomToolTip();

    // ----------------------------------------------------------------------------
    // Start writing your code from here...
    // ----------------------------------------------------------------------------
    ShowCustomToolTip(L"Initializing AltTab...", 1000);

    // Load settings from AltTabSettings.ini file
    ATLoadSettings();

    // If we're relaunching for elevation, allow it
    if (g_GeneralSettings.IsProcessElevated && !g_GeneralSettings.IsTaskElevated && wcsstr(GetCommandLineW(), L"--elevated")) {
        AT_LOG_INFO("Relaunching with elevated privileges (--elevated argument).");
        g_GeneralSettings.IsTaskElevated = true;
        if (g_GeneralSettings.IsRunAtStartup) {
            RunAtStartup(true, true);
        }
    }

    // From 2025.1.0.0, we are not going to create a registry key for RunAtStartup.
    // We always create a task in task scheduler to run AltTab at startup. So, we can simply remove the registry key if
    // it exists.
    RemoveStartupFromRegistry();

#if 0
    // Run At Startup
    if (!g_GeneralSettings.IsRunAtStartup) {
        RunAtStartup(true, g_GeneralSettings.IsElevated);
    }
#endif // _DEBUG

    // Register AltTab window class
    if (!RegisterAltTabWindow()) {
        std::wstring info = L"Failed to register AltTab window class.";
        AT_LOG_ERROR("Failed to register AltTab window class.");
        MessageBox(nullptr, info.c_str(), AT_PRODUCT_NAMEW, MB_OK | MB_ICONERROR);
        return 1;
    }

    // System tray
    // Create a hidden window for tray icon handling
    g_hMainWnd = CreateMainWindow(hInstance);

	 HINSTANCE hinstUser32 = LoadLibrary(L"user32.dll");
    if (hinstUser32) {
       g_pfnIsHungAppWindow = (IsHungAppWindowFunc)GetProcAddress(hinstUser32, "IsHungAppWindow");
    }

    // Add the tray icon
    if (g_Settings.SystemTrayIconEnabled && !AddNotificationIcon(g_hMainWnd)) {
        const std::wstring info = L"Failed to add AltTab tray icon.";
        AT_LOG_ERROR("Failed to add AltTab tray icon.");
        ShowCustomToolTip(info, 3000);

        // Try to restart the application after 3 seconds
        Sleep(3000);
        RestartAltTab();
    }

    g_KeyboardHook = SetWindowsHookEx(WH_KEYBOARD_LL, LLKeyboardProc, hInstance, NULL);

    HACCEL hAccelTable = LoadAccelerators(hInstance, MAKEINTRESOURCE(IDC_ALTTAB));

    // Check for updates
    if (g_Settings.CheckForUpdatesOpt == L"Startup") {
        std::thread thr(CheckForUpdates, true);
        thr.detach();
    } else if (g_Settings.CheckForUpdatesOpt != L"Never") {
        // Check for every 1 hour
        const UINT elapse = 3600000;
        SetTimer(g_hMainWnd, TIMER_CHECK_FOR_UPDATES, elapse, CheckForUpdatesTimerCB);
    }

    MSG msg;

    // Main message loop:
    while (GetMessage(&msg, nullptr, 0, 0)) {
        if (!TranslateAccelerator(msg.hwnd, hAccelTable, &msg)) {
            TranslateMessage(&msg);
            DispatchMessage(&msg);
        }
    }

    UnhookWindowsHookEx(g_KeyboardHook);

    // Un-initialize the common things like common controls, GDI+ etc.
    UninitializeCOM();
    ShutdownGDIPlus();

    return (int) msg.wParam;
}

/**
 * AltTab system tray icon procedure
 * 
 * \param hWnd      hWnd
 * \param message   message
 * \param wParam    wParam
 * \param lParam    lParam
 * 
 * \return 
 */
LRESULT CALLBACK MainWndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam) {
    //AT_LOG_TRACE;
    switch (message) {
    case WM_COMMAND: {
        int const wmId = LOWORD(wParam);
        // Parse the menu selections:
        switch (wmId) {
        case ID_TRAYCONTEXTMENU_ABOUTALTTAB:
            AT_LOG_INFO("ID_TRAYCONTEXTMENU_ABOUTALTTAB");
            DialogBox(g_hInstance, MAKEINTRESOURCE(IDD_ABOUTBOX), hWnd, ATAboutDlgProc);
            break;

        case ID_TRAYCONTEXTMENU_README:
            AT_LOG_INFO("ID_TRAYCONTEXTMENU_README");
            break;

        case ID_TRAYCONTEXTMENU_HELP:
            AT_LOG_INFO("ID_TRAYCONTEXTMENU_HELP");
            break;

        case ID_TRAYCONTEXTMENU_RELEASENOTES:
            AT_LOG_INFO("ID_TRAYCONTEXTMENU_RELEASENOTES");
            break;

        case ID_TRAYCONTEXTMENU_SETTINGS:
            AT_LOG_INFO("ID_TRAYCONTEXTMENU_SETTINGS");
            DialogBoxW(g_hInstance, MAKEINTRESOURCE(IDD_SETTINGS), g_hMainWnd, ATSettingsDlgProc);
            break;

        case ID_TRAYCONTEXTMENU_DISABLEALTTAB:
            AT_LOG_INFO("ID_TRAYCONTEXTMENU_DISABLEALTTAB");
            break;

        case ID_TRAYCONTEXTMENU_CHECKFORUPDATES:
            AT_LOG_INFO("ID_TRAYCONTEXTMENU_CHECKFORUPDATES");
            break;

        case ID_TRAYCONTEXTMENU_RUNATSTARTUP:
            AT_LOG_INFO("ID_TRAYCONTEXTMENU_RUNATSTARTUP");
            break;

        case ID_TRAYCONTEXTMENU_EXIT:
            AT_LOG_INFO("ID_TRAYCONTEXTMENU_EXIT");
            PostQuitMessage(0);
            break;
        }

    } break;

    case WM_USER_ALTTAB_TRAYICON: {
        const auto wmId = LOWORD(lParam);
        //AT_LOG_INFO("WM_USER_ALTTAB_TRAYICON: wParam = %d, lParam = %d, wmId: %#06x", wParam, lParam, wmId);
        switch (wmId) {
        case WM_RBUTTONUP: {
            AT_LOG_INFO("WM_USER_ALTTAB_TRAYICON: WM_RBUTTONUP");
            POINT pt;
            GetCursorPos(&pt);
            ShowTrayContextMenu(hWnd, pt);
        } break;

        case WM_LBUTTONUP: {
            AT_LOG_INFO("WM_USER_ALTTAB_TRAYICON: WM_LBUTTONUP");
            // Set g_IsAltCtrlTab to true to add the windows to AltTab window, otherwise no windows will be added.
            g_IsAltCtrlTab = true;
            ShowAltTabWindow(g_hAltTabWnd, 0);
        } break;

        //case WM_MOUSEMOVE: {
        //    AT_LOG_INFO("WM_MOUSEMOVE");
        //} break;
        }
    } break;
    
    case WM_DESTROY:
        AT_LOG_INFO("WM_DESTROY");
        KillTimer(hWnd, TIMER_CHECK_FOR_UPDATES);
        break;

    default:
        return DefWindowProc(hWnd, message, wParam, lParam);
    }
    return 0;
}

// Function to create a hidden window for tray icon handling
HWND CreateMainWindow(HINSTANCE hInstance) {
    HWND hwndFrgnd = GetForegroundWindow();
    DWORD idThreadAttachTo = hwndFrgnd ? GetWindowThreadProcessId(hwndFrgnd, NULL) : 0;
    if (idThreadAttachTo) {
        AttachThreadInput(GetCurrentThreadId(), idThreadAttachTo, TRUE);
    }
    WNDCLASS wc      = { 0 };
    wc.lpfnWndProc   = MainWndProc;
    wc.hInstance     = hInstance;
    wc.lpszClassName = L"__AltTab_MainWndCls__";
    wc.hIcon         = LoadIcon(g_hInstance, MAKEINTRESOURCE(IDI_ALTTAB));
    RegisterClass(&wc);

    return CreateWindowW(
        wc.lpszClassName,       // Class Name
        L"AltTabMainWindow",    // Window Name
        0,                      // Style
        0,                      // X
        0,                      // Y
        1,                      // Width
        1,                      // Height
        nullptr,                // Parent
        nullptr,                // Menu
        hInstance,              // Instance
        nullptr                 // Extra
    );
}

BOOL AddNotificationIcon(HWND hWndTrayIcon) {
    // Set up the NOTIFYICONDATA structure
    NOTIFYICONDATA nid   = { 0 };
    nid.cbSize           = sizeof(NOTIFYICONDATA);
    nid.hWnd             = hWndTrayIcon;
    nid.uID              = 1;
    nid.uFlags           = NIF_ICON | NIF_TIP | NIF_MESSAGE | NIF_SHOWTIP;
    nid.uCallbackMessage = WM_USER_ALTTAB_TRAYICON;
    nid.hIcon            = LoadIcon(g_hInstance, MAKEINTRESOURCE(IDI_ALTTAB));

    wcscpy_s(nid.szTip, AT_PRODUCT_NAMEW L" v" AT_VERSION_TEXTW); // Tooltip text on mouse hover

    return Shell_NotifyIcon(NIM_ADD, &nid);
}

// ----------------------------------------------------------------------------
// Low level keyboard procedure
// 
// If nCode is less than zero:
//   the hook procedure must return the value returned by CallNextHookEx.
// 
// If nCode is greater than or equal to zero:
//   and the hook procedure did not process the message, it is highly
//   recommended that you call CallNextHookEx and return the value it returns;
//   otherwise, other applications that have installed WH_KEYBOARD_LL hooks 
//   will not receive hook notifications and may behave incorrectly as a result.
// 
// If the hook procedure processed the message:
//   it may return a nonzero value to prevent the system from passing the
//   message to the rest of the hook chain or the target window procedure.
// ----------------------------------------------------------------------------
LRESULT CALLBACK LLKeyboardProc(int nCode, WPARAM wParam, LPARAM lParam) {
    //AT_LOG_TRACE;

    // Call the next hook in the chain
    if (nCode != HC_ACTION) {
        return CallNextHookEx(g_KeyboardHook, nCode, wParam, lParam);
    }
    
    //AT_LOG_INFO(std::format("hAltTabWnd is nullptr: {}", hAltTabWnd == nullptr).c_str());
    auto* pKeyboard = reinterpret_cast<KBDLLHOOKSTRUCT*>(lParam);
    DWORD vkCode    = pKeyboard->vkCode;
        
    // Check if Alt key is pressed
    bool isAltPressed  = GetAsyncKeyState(VK_MENU   ) & 0x8000;
    bool isCtrlPressed = GetAsyncKeyState(VK_CONTROL) & 0x8000;

    // When Alt is pressed down WM_SYSKEYDOWN is sent and when Alt is released WM_KEYUP is sent.
    // But, GetAsyncKeyState return 0 when Alt is pressed down first time and returns 1 while holding down.
    // So, we need to check vkCode also to make it work for the first time also.
    isAltPressed = isAltPressed || vkCode == VK_LMENU || vkCode == VK_RMENU || vkCode == VK_MENU;

    //AT_LOG_DEBUG("wParam: %#x, vkCode: %0#4x, isAltPressed: %d", wParam, vkCode, isAltPressed);

    // ----------------------------------------------------------------------------
    // Alt key is pressed
    // 20240316: Now we are handling Alt+Ctrl+Tab also. Alt key will be released
    // when user wants Alt+Ctrl+Tab window. So, isAltPressed is not always true.
    // Hence, check for g_hAltTabWnd also whether the AltTab window is displayed.
    // ----------------------------------------------------------------------------
    if (isAltPressed && (!isCtrlPressed || g_Settings.HKAltCtrlTabEnabled) || g_hAltTabWnd != nullptr) {
        //AT_LOG_INFO("Alt key pressed!, wParam: %#x", wParam);

        // Check if Shift key is pressed
        bool isShiftPressed = GetAsyncKeyState(VK_SHIFT) & 0x8000;
        int  direction      = isShiftPressed ? -1 : 1;

        if (wParam == WM_KEYDOWN || wParam == WM_SYSKEYDOWN) {
            if (g_hAltTabWnd == nullptr) {
                // Check if windows native Alt+Tab window is displayed. If so, do not process the message.
                // Otherwise, both native Alt+Tab window and AltTab window will be displayed.
                bool isNativeATWDisplayed = IsNativeATWDisplayed();
                //AT_LOG_INFO("isNativeATWDisplayed: %d", isNativeATWDisplayed);

                // ----------------------------------------------------------------------------
                // Alt + Tab / Alt + Ctrl + Tab
                // ----------------------------------------------------------------------------
                bool showAltTabWindow =
                   (g_Settings.HKAltTabEnabled     && !isCtrlPressed) ||
                   (g_Settings.HKAltCtrlTabEnabled &&  isCtrlPressed);
                if (showAltTabWindow && vkCode == VK_TAB) {
                    if (isNativeATWDisplayed) {
                        //AT_LOG_INFO("isNativeATWDisplayed: %d", isNativeATWDisplayed);
                        return CallNextHookEx(g_KeyboardHook, nCode, wParam, lParam);
                    }

                    g_IsAltTab      = true;
                    g_IsAltBacktick = false;

                    if (!isShiftPressed) {
                        // Alt+Tab is pressed
                        AT_LOG_INFO("--------- Alt+Tab Pressed! ---------");
                        ShowAltTabWindow(g_hAltTabWnd, 1);
                    } else {
                        // Alt+Shift+Tab is pressed
                        AT_LOG_INFO("--------- Alt+Shift+Tab Pressed! ---------");
                        ShowAltTabWindow(g_hAltTabWnd, -1);
                    }
                }
                // ----------------------------------------------------------------------------
                // Alt + Backtick
                // ----------------------------------------------------------------------------
                else if (g_Settings.HKAltBacktickEnabled && vkCode == g_Settings.HKBacktickKey) {

                    g_IsAltTab      = false;
                    g_IsAltBacktick = true;

                    if (!isShiftPressed) {
                        // Alt+Backtick is pressed
                        AT_LOG_INFO("--------- Alt+Backtick Pressed! ---------");
                        ShowAltTabWindow(g_hAltTabWnd, 1);
                    } else {
                        // Alt+Shift+Backtick is pressed
                        AT_LOG_INFO("--------- Alt+Shift+Backtick Pressed! ---------");
                        ShowAltTabWindow(g_hAltTabWnd, -1);
                    }
                }
                // ----------------------------------------------------------------------------
                // Check if Alt key is pressed twice within 500ms
                // ----------------------------------------------------------------------------
                //else if (vkCode == VK_LMENU || vkCode == VK_RMENU) {
                //    DWORD currentTime = GetTickCount();
                //    AT_LOG_INFO("currentTime: %u, g_LastAltKeyPressTime: %u", currentTime, g_LastAltKeyPressTime);

                //    if (!g_IsAltKeyPressed || (currentTime - g_LastAltKeyPressTime) > 500) {
                //        g_IsAltKeyPressed = true;
                //        g_LastAltKeyPressTime = currentTime;
                //    } else {
                //        // Alt key pressed twice quickly!
                //        AT_LOG_INFO("Alt key pressed twice within 500ms!");
                //        g_IsAltKeyPressed = false;
                //        g_LastAltKeyPressTime = 0;
                //    }
                //}

                // ----------------------------------------------------------------------------
                // Create timer here to check the Alt key is released.
                // ----------------------------------------------------------------------------
                if (g_IsAltTab || g_IsAltBacktick) {
                    // Do NOT start the timer if Ctrl key is pressed.
                    // Here, when user presses Alt+Ctrl+Tab, AltTab window remains open.
                    if (!g_Settings.HKAltCtrlTabEnabled || !isCtrlPressed) {
                        SetTimer(g_hMainWnd, TIMER_CHECK_ALT_KEYUP, 50, CheckAltKeyIsReleased);
                    } else {
                        g_IsAltCtrlTab = true;
                    }
                    return TRUE;
                }
            } else {
                // ----------------------------------------------------------------------------
                // AltTab window is displayed.
                // ----------------------------------------------------------------------------
                if (vkCode == VK_TAB) {
                    //AT_LOG_INFO("Tab Pressed!");
                    ShowAltTabWindow(g_hAltTabWnd, direction);
                    return TRUE;
                } 
                
                // We need to handle the `Alt + Escape` key to close the AltTab window, otherwise
                // default behavior of 'Alt + Escape' will be executed which is switching to the
                // next window in the Z order.
                if (vkCode == VK_ESCAPE) {
                    AT_LOG_INFO("Escape Pressed!");

                    // First check if there is any context menu is displayed.
                    if (g_hContextMenu != nullptr) {
                        // Actually here we need to destroy the context menu.
                        // Get active window and send VK_ESCAPE to it.
                        //HWND hActiveWnd = GetForegroundWindow();
                        //AT_LOG_INFO("Sending WM_CLOSE to context menu window: %#010x", (UINT_PTR)hActiveWnd);
                        //PostMessageW(hActiveWnd, WM_KEYDOWN, VK_ESCAPE, 0);
                        //g_hContextMenu = nullptr;
                        DestroyContextMenu();
                        return TRUE;
                    }

                    DestroyAltTabWindow();
                    return TRUE;
                }

                if (vkCode == VK_APPS && g_SelectedIndex != -1) {
                    AT_LOG_INFO("Apps Pressed!");
                    ShowContextMenuAtItemCenter();
                    return TRUE;
                }

                // If any other application define the HotKey Alt + ~, we need
                // to stop sending WM_KEYDOWN on ~ to that application.
                // For example. If AltTabAlternative is running, ATA's window
                // gets opened.
                //
                // Now, send WM_KEYDOWN to g_hAltTabWnd, there handle.
                if (vkCode == g_Settings.HKBacktickKey) {
                    //AT_LOG_INFO("Backtick Pressed!");
                    PostMessage(g_hListView, WM_KEYDOWN, vkCode, 0);
                    return TRUE;
                }

                // This is exceptional case to handle Alt + Space
                // `PowerToys` also uses `Alt + Space` hotkey. So, prevent this hotkey while AltTab window is open.
                // If we do not prevent this, AltTab window will be closed and PowerToys Run window will be opened.
                if (vkCode == VK_SPACE /* Space */) {
                    PostMessageW(g_hSearchString, WM_KEYDOWN, vkCode, 0);
                    return TRUE;
                }

                if (vkCode == VK_F4) {
                    // DO NOT DESTROY ALT TAB
                    return TRUE;
                } 

                //AT_LOG_WARN("Not Handled: wParam: %u, vkCode: %0#4x, isprint: %d", wParam, vkCode, iswprint(vkCode));
            }
        //} else if (wParam == WM_KEYUP || wParam == WM_SYSKEYUP) {
        //   if (vkCode == VK_LMENU || vkCode == VK_RMENU) {
        //        g_IsAltKeyPressed = false;
        //        g_LastAltKeyPressTime = 0;
        //    }
        }
    } // if (isAltPressed || g_hAltTabWnd != nullptr)

#if 0
    // This is exceptional case to handle Alt + Space
    // `PowerToys` also uses `Alt + Space` hotkey. So, prevent this hotkey while AltTab window is open.
    // If we do not prevent this, AltTab window will be closed and PowerToys Run window will be opened.
    if (g_hAltTabWnd != nullptr && vkCode == VK_SPACE /* Space */) {
         PostMessageW(g_hSearchString, WM_KEYDOWN, vkCode, 0);
         return TRUE;
    }
#endif // 0

    //AT_LOG_DEBUG("CallNextHookEx(g_KeyboardHook, nCode, wParam, lParam);");
    return CallNextHookEx(g_KeyboardHook, nCode, wParam, lParam);
}

// Timer callback function
void CALLBACK CheckForUpdatesTimerCB(HWND /*hWnd*/, UINT /*uMsg*/, UINT_PTR /*idEvent*/, DWORD /*dwTime*/) {
    AT_LOG_TRACE;
    std::wstring frequency  = g_Settings.CheckForUpdatesOpt;
    auto lastCheckTimestamp = ReadLastCheckForUpdatesTS();
    auto currentTimeStamp   = std::chrono::system_clock::now();
    auto timeDiff           = currentTimeStamp - lastCheckTimestamp;

    if ((frequency == L"Daily"  && timeDiff >= std::chrono::hours(24    )) ||
        (frequency == L"Weekly" && timeDiff >= std::chrono::hours(24 * 7)))
    {
        // Perform the update check logic here
        CheckForUpdates(true);

        // Update the last check timestamp
        WriteCheckForUpdatesTS(currentTimeStamp);
    } else {
        AT_LOG_INFO("Update check not required at this time.");
    }
}

void CALLBACK CheckAltKeyIsReleased(HWND /*hWnd*/, UINT /*uMsg*/, UINT_PTR /*idEvent*/, DWORD /*dwTime*/) {
    //AT_LOG_TRACE;
    bool isAltPressed = GetAsyncKeyState(VK_MENU) & 0x8000;
    if (g_hAltTabWnd && !isAltPressed) {
        // Alt key released, destroy your window
        AT_LOG_INFO("--------- Alt key released! ---------");
        DestroyAltTabWindow(true);
    }
}

void TrayContextMenuItemHandler(HWND /*hWnd*/, HMENU hSubMenu, UINT menuItemId) {
    // Commented out the hWnd parameter since TrayContextMenuItemHandler is invoked from tray icon and
    // AltTab window search string context menu as well. So, hWnd is not always g_hMainWnd.
    switch (menuItemId) {
    case ID_TRAYCONTEXTMENU_ABOUTALTTAB:
        AT_LOG_INFO("ID_TRAYCONTEXTMENU_ABOUTALTTAB");
        DialogBox(g_hInstance, MAKEINTRESOURCE(IDD_ABOUTBOX), g_hMainWnd, ATAboutDlgProc);
        break;

    case ID_TRAYCONTEXTMENU_README:
        AT_LOG_INFO("ID_TRAYCONTEXTMENU_README");
        ShowReadMeWindow();
        break;

    case ID_TRAYCONTEXTMENU_HELP:
        AT_LOG_INFO("ID_TRAYCONTEXTMENU_HELP");
        ShowHelpWindow();
        break;

    case ID_TRAYCONTEXTMENU_RELEASENOTES:
        AT_LOG_INFO("ID_TRAYCONTEXTMENU_RELEASENOTES");
        ShowReleaseNotesWindow();
        break;

    case ID_TRAYCONTEXTMENU_SETTINGS:
        AT_LOG_INFO("ID_TRAYCONTEXTMENU_SETTINGS");
        DialogBoxW(g_hInstance, MAKEINTRESOURCE(IDD_SETTINGS), g_hMainWnd, ATSettingsDlgProc);
        break;

    case ID_TRAYCONTEXTMENU_DISABLEALTTAB: {
        AT_LOG_INFO("ID_TRAYCONTEXTMENU_DISABLEALTTAB");
        UINT checkState          = GetCheckState(hSubMenu, menuItemId);
        bool disableAltTab       = !(checkState == MF_CHECKED);
        g_Settings.DisableAltTab = disableAltTab;

        if (disableAltTab) {
            UnhookWindowsHookEx(g_KeyboardHook);
        } else {
            g_KeyboardHook = SetWindowsHookEx(WH_KEYBOARD_LL, LLKeyboardProc, g_hInstance, NULL);
        }
    }
    break;

    case ID_TRAYCONTEXTMENU_CHECKFORUPDATES: {
        AT_LOG_INFO("ID_TRAYCONTEXTMENU_CHECKFORUPDATES");
        ShowCustomToolTip(L"Checking for updates..., please wait.");

        // Had to run CheckForUpdates in a thread to display the tooltip... :-(
        std::thread thr(CheckForUpdates, false); thr.detach();
    }
    break;

    case ID_TRAYCONTEXTMENU_RUNATSTARTUP: {
        AT_LOG_INFO("ID_TRAYCONTEXTMENU_RUNATSTARTUP");
        UINT checkState = GetCheckState(hSubMenu, menuItemId);
        const bool checked    = (checkState == MF_CHECKED);
        RunAtStartup(!checked, g_GeneralSettings.IsProcessElevated);
        ToggleCheckState(hSubMenu, menuItemId);
    }
    break;

    case ID_TRAYCONTEXTMENU_RUNASADMIN: {
        AT_LOG_INFO("ID_TRAYCONTEXTMENU_RUNASADMIN");
        const UINT checkState = GetCheckState(hSubMenu, menuItemId);
        const bool checked = (checkState == MF_CHECKED);
        AT_LOG_INFO("Run as admin: %d", checked);

        // Check if the current process is elevated
        // If the current process is elevated, then we don't need to relaunch AltTab with administrator privileges.
        //if (checked && g_GeneralSettings.IsProcessElevated) {
        //    AT_LOG_INFO("AltTab is already running with administrator privileges.");
        //    ShowCustomToolTip(L"AltTab is already running with administrator privileges.", 3000);
        //    return;
        //}

        const bool isRunAtStartup = IsRunAtStartup();
        AT_LOG_INFO("IsChecked: %d, Run at startup: %d", checked, isRunAtStartup);
        if (!checked ) {
            // Also check if `Run at Startup` is enabled. Then, we need to create a task in `Task Scheduler` to run
            // AltTab at windows log on to run `AltTab` with highest privileges.
            if (isRunAtStartup) {
                // If the process is already elevated, no need to relaunch as administrator privileges.
                if (g_GeneralSettings.IsProcessElevated) {
                    AT_LOG_INFO("AltTab is already running with administrator privileges.");
                    DeleteAutoStartTask();
                    CreateAutoStartTask(true);
                    return;
                }
                
                // Ask user for confirmation to always run as administrator since `Run at Startup` is enabled.
                const int result = MessageBoxW(
                    nullptr,
                    L"Do you want to always run AltTab as Administrator since 'Run at Startup' is enabled?\n\n"
                    L"This will change the AltTab startup task to 'Run with highest privileges' option in Task Scheduler.\n"
                    L"Going to relaunch AltTab with admin privileges...",
                    AT_PRODUCT_NAMEW,
                    MB_YESNO | MB_ICONQUESTION | MB_DEFBUTTON1);
                  AT_LOG_INFO("Relaunching AltTab as admin, elevated: %d", result);
                if (result == IDYES) {
                    RelaunchAsAdminAndExit(true, true);
                } else {
                    //RelaunchAsAdminAndExit(false, false);
                    // Do NOTHING
                }
            } else {
                RelaunchAsAdminAndExit(true, false);
            }
        } else {
            if (isRunAtStartup) {
                AT_LOG_INFO("Delete task for Run as admin...");
                DeleteAutoStartTask();
                AT_LOG_INFO("Creating task for without admin...");
                CreateAutoStartTask(false);
            } else {
                RelaunchAsAdminAndExit(false, false);
            }
        }
        ToggleCheckState(hSubMenu, menuItemId);
    } break;

    case ID_TRAYCONTEXTMENU_CLOSEALLWINDOWS: {
        // Get AltTab windows
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
                // Ignore Visual Studio IDEs in debug mode
                if (EqualsIgnoreCase(windowData.ProcessName, L"devenv.exe")
                    || EqualsIgnoreCase(windowData.ProcessName, L"Code.exe")
                    || EqualsIgnoreCase(windowData.ProcessName, L"Outlook.exe")
                    || EqualsIgnoreCase(windowData.ProcessName, L"xplorer2_64.exe")
                    || EqualsIgnoreCase(windowData.ProcessName, L"msedge.exe")
                    || EqualsIgnoreCase(windowData.ProcessName, L"OneNote.exe")
                    || EqualsIgnoreCase(windowData.ProcessName, L"Fork.exe")
                    || EqualsIgnoreCase(windowData.ProcessName, L"ConEmu64.exe")
                    || EqualsIgnoreCase(windowData.ProcessName, L"ms-teams.exe")
                    || EqualsIgnoreCase(windowData.ProcessName, L"AltTab.exe")) {
                    AT_LOG_INFO(
                        "Ignoring: ProcessName: [%-15ls], Title:[%ls] ...",
                        windowData.ProcessName.c_str(),
                        windowData.Title.c_str());
                    continue;
                }
#endif // _DEBUG
                AT_LOG_INFO(
                    "Closing : ProcessName: [%-15ls], Title:[%ls] ...",
                    windowData.ProcessName.c_str(),
                    windowData.Title.c_str());
                PostMessage(windowData.hWnd, WM_CLOSE, 0, 0);
            }
        }
    } break;

    case ID_TRAYCONTEXTMENU_RELOADALTTABSETTINGS: {
        AT_LOG_INFO("ID_TRAYCONTEXTMENU_RELOADALTTABSETTINGS");
        ATLoadSettings();
        ShowCustomToolTip(L"Settings reloaded successfully.", 3000);
    } break;

    case ID_TRAYCONTEXTMENU_RESTART: {
        AT_LOG_INFO("ID_TRAYCONTEXTMENU_RESTART");
        RestartAltTab();
    }
    break;

    case ID_TRAYCONTEXTMENU_EXIT:
        AT_LOG_INFO("ID_TRAYCONTEXTMENU_EXIT");
        PostQuitMessage(0);
        //int result = MessageBoxW(
        //    hWnd,
        //    L"Are you sure you want to exit?",
        //    AT_PRODUCT_NAMEW,
        //    MB_OKCANCEL | MB_ICONQUESTION | MB_DEFBUTTON2);
        //if (result == IDOK) {
        //    PostQuitMessage(0);
        //}
        break;
    }
}

// ----------------------------------------------------------------------------
// Show AltTab system tray context menu
// ----------------------------------------------------------------------------
bool ShowTrayContextMenu(HWND hWnd, POINT pt) {
    // Update general settings
    // Note: The current process is elevated but `RunAtStartup` is not enabled then IsRunElevated will be false.
    g_GeneralSettings = GetGeneralSettings();
    AT_LOG_INFO(
        "GeneralSettings: IsProcessElevated = %d, IsTaskElevated = %d, IsRunAtStartup = %d",
        g_GeneralSettings.IsProcessElevated,
        g_GeneralSettings.IsTaskElevated,
        g_GeneralSettings.IsRunAtStartup);

    HMENU hMenu = LoadMenu(g_hInstance, MAKEINTRESOURCE(IDC_TRAY_CONTEXTMENU));
    UINT menuItemId = 0;
    if (hMenu) {
        HMENU hSubMenu = GetSubMenu(hMenu, 0);
        if (hSubMenu) {
            // Our window must be foreground before calling TrackPopupMenu or
            // the menu will not disappear when the user clicks away
            SetForegroundWindow(hWnd);
            const bool isRunAtStartup = IsRunAtStartup();
            if (isRunAtStartup) {
                AT_LOG_INFO("Run at startup is enabled.");
                SetCheckState(hSubMenu, ID_TRAYCONTEXTMENU_RUNATSTARTUP, MF_CHECKED);
            }

            // Change the `Run as Administrator` menu item text to `Always run as Administrator` if AltTab is running at
            // startup.
            if (isRunAtStartup) {
                MENUITEMINFOW mii = { sizeof(mii) };
                mii.fMask = MIIM_STRING;
                mii.dwTypeData = (LPWSTR)L"Always run as Administrator";
                SetMenuItemInfoW(hMenu, ID_TRAYCONTEXTMENU_RUNASADMIN, FALSE, &mii);
            } else {
                MENUITEMINFOW mii = { sizeof(mii) };
                mii.fMask = MIIM_STRING;
                mii.dwTypeData = (LPWSTR)L"Run as Administrator";
                SetMenuItemInfoW(hMenu, ID_TRAYCONTEXTMENU_RUNASADMIN, FALSE, &mii);
            }

            if (g_GeneralSettings.IsProcessElevated) {
                if (isRunAtStartup) {
                    if (IsTaskRunWithHighestPrivileges()) {
                        SetCheckState(hSubMenu, ID_TRAYCONTEXTMENU_RUNASADMIN, MF_CHECKED);
                    }
                    else {
                        SetCheckState(hSubMenu, ID_TRAYCONTEXTMENU_RUNASADMIN, MF_UNCHECKED);
                    }
                } else {
                    SetCheckState(hSubMenu, ID_TRAYCONTEXTMENU_RUNASADMIN, MF_CHECKED);
                }

                // If the current process is elevated and RunAtStartup is NOT checked then disable
                // `Run as Administrator`, because we can't run a non-elevated process from the elevated process.
                if (!isRunAtStartup) {
                    EnableMenuItem(hSubMenu, ID_TRAYCONTEXTMENU_RUNASADMIN, MF_BYCOMMAND | MF_DISABLED | MF_GRAYED);
                } else {
                    EnableMenuItem(hSubMenu, ID_TRAYCONTEXTMENU_RUNASADMIN, MF_BYCOMMAND | MF_ENABLED);
                }
            }

            if (g_Settings.DisableAltTab) {
                SetCheckState(hSubMenu, ID_TRAYCONTEXTMENU_DISABLEALTTAB, MF_CHECKED);
            }

            // Respect menu drop alignment
            UINT uFlags = TPM_RIGHTBUTTON;
            if (GetSystemMetrics(SM_MENUDROPALIGNMENT) != 0) {
                uFlags |= TPM_RIGHTALIGN;
            } else {
                uFlags |= TPM_LEFTALIGN;
            }

            // Set the global context menu handle
            g_hContextMenu = hSubMenu;

            // Use TPM_RETURNCMD flag let TrackPopupMenuEx function return the 
            // menu item identifier of the user's selection in the return value.
            uFlags |= TPM_RETURNCMD;
            menuItemId = TrackPopupMenuEx(hSubMenu, uFlags, pt.x, pt.y, hWnd, nullptr);
            if (menuItemId != 0) {
                // First, reset the global context menu handle before handling the menu item,
                // so that other parts of the code won't be blocked.
                g_hContextMenu = nullptr;

                TrayContextMenuItemHandler(hWnd, hSubMenu, menuItemId);
            }
        }
        DestroyMenu(hMenu);
    }
    return menuItemId != 0;
}

void ToggleCheckState(HMENU hMenu, UINT menuItemID) {
    MENUITEMINFO menuItemInfo = { sizeof(MENUITEMINFO) };
    menuItemInfo.cbSize       = sizeof(MENUITEMINFO);
    menuItemInfo.fMask        = MIIM_STATE;

    // Get the current state of the menu item
    GetMenuItemInfo(hMenu, menuItemID, FALSE, &menuItemInfo);

    // Toggle the check mark state
    menuItemInfo.fState ^= MF_CHECKED;

    // Set the updated state
    SetMenuItemInfo(hMenu, menuItemID, FALSE, &menuItemInfo);
}

UINT GetCheckState(HMENU hMenu, UINT menuItemID) {
    MENUITEMINFO menuItemInfo = { sizeof(MENUITEMINFO) };
    menuItemInfo.cbSize       = sizeof(MENUITEMINFO);
    menuItemInfo.fMask        = MIIM_STATE;

    // Get the current state of the menu item
    GetMenuItemInfo(hMenu, menuItemID, FALSE, &menuItemInfo);

    // Toggle the check mark state
    return menuItemInfo.fState & MF_CHECKED;
}

void SetCheckState(HMENU hMenu, UINT menuItemID, UINT fState) {
    MENUITEMINFO menuItemInfo = { sizeof(MENUITEMINFO) };
    menuItemInfo.cbSize       = sizeof(MENUITEMINFO);
    menuItemInfo.fMask        = MIIM_STATE;
    menuItemInfo.fState       = fState;

    // Set the updated state
    SetMenuItemInfo(hMenu, menuItemID, FALSE, &menuItemInfo);
}

bool RunAtStartup(const bool runAtStartup, const bool withHighestPrivileges) {
    const bool isElevated = g_GeneralSettings.IsProcessElevated;
    AT_LOG_INFO("runAtStartup: %d, IsProcessElevated: %d", runAtStartup, isElevated);

    HKEY hKey;
    LONG result = RegOpenKeyEx(
        HKEY_CURRENT_USER,
        L"Software\\Microsoft\\Windows\\CurrentVersion\\Run",
        0,
        KEY_SET_VALUE,
        &hKey);

    // First, always delete the registry entry if it exists. And, we are going to use Task Scheduler to run AltTab at
    // startup.
    if (result == ERROR_SUCCESS) {
         result = RegDeleteValue(hKey, AT_PRODUCT_NAMEW);
         RegCloseKey(hKey);
    }

    bool succeeded = false;
    if (runAtStartup) {
        DeleteAutoStartTask();
        if (isElevated && withHighestPrivileges) {
            succeeded = CreateAutoStartTask(true);
            if (succeeded) {
                AT_LOG_INFO("Run at startup enabled with highest privileges.");
            } else {
                AT_LOG_INFO("Failed to create task for Run at startup with highest privileges.");
            }
        } else {
            succeeded = CreateAutoStartTask(false);
            if (succeeded) {
                AT_LOG_INFO("Run at startup enabled without highest privileges.");
            } else {
                AT_LOG_INFO("Failed to create task for Run at startup without highest privileges.");
            }
        }
    } else {
        // If runAtStartup is false, delete the task in Task Scheduler if it exists.
        if (IsAutoStartTaskActive()) {
            succeeded = DeleteAutoStartTask();
        } else {
            AT_LOG_INFO("No task found in Task Scheduler for Run at startup.");
            succeeded = true; // No task to delete, so return true.
        }
    }

    return succeeded;
}

bool IsRunAtStartup() {
    // First check if there is an task in Task Scheduler
    if (IsAutoStartTaskActive()) {
        AT_LOG_INFO("Auto start task exists for this user.");
        return true;
    }

    HKEY hKey;
    LONG result = RegOpenKeyEx(
        HKEY_CURRENT_USER,
        L"Software\\Microsoft\\Windows\\CurrentVersion\\Run",
        0,
        KEY_READ,
        &hKey);

    if (result == ERROR_SUCCESS) {
       // Get AltTab value from the registry
        AT_LOG_INFO("Checking registry for Run at startup...");

        // Check if the registry entry exists

        DWORD dataType = 0;
        DWORD dataSize = 0;

        // First call to get the required buffer size
        result = RegQueryValueExW(hKey, AT_PRODUCT_NAMEW, nullptr, &dataType, nullptr, &dataSize);
        if (result != ERROR_SUCCESS || dataType != REG_SZ) {
            RegCloseKey(hKey);
            return false;
        }

        std::wstring value(dataSize / sizeof(wchar_t), L'\0');
        result =
            RegQueryValueExW(hKey, AT_PRODUCT_NAMEW, nullptr, nullptr, reinterpret_cast<LPBYTE>(&value[0]), &dataSize);
        RegCloseKey(hKey);

        if (result == ERROR_SUCCESS && dataSize > 0) {
            // The registry entry exists, so AltTab is set to run at startup
            AT_LOG_INFO("Run at startup is enabled in registry.");

            // This is old mechanism, so going to delete the registry entry and create a task in Task Scheduler
            if (g_GeneralSettings.IsProcessElevated) {
                // Create a task in Task Scheduler to run AltTab with highest privileges
                if (CreateAutoStartTask(true)) {
                    AT_LOG_INFO("Run at startup enabled with highest privileges.");
                } else {
                    AT_LOG_ERROR("Failed to create task for Run at startup with highest privileges.");
                }
            } else {
                if (CreateAutoStartTask(false)) {
                    AT_LOG_INFO("Run at startup enabled without highest privileges.");
                } else {
                    AT_LOG_ERROR("Failed to create task for Run at startup without highest privileges.");
                }
            }

            return true;
        }

        if (result == ERROR_FILE_NOT_FOUND) {
            // The registry entry does not exist, so AltTab is not set to run at startup
            AT_LOG_INFO("Run at startup is NOT enabled.");
        } else {
            AT_LOG_ERROR("Failed to query the registry value for Run at startup.");
        }
    }
    return false;
}

// ----------------------------------------------------------------------------
// This function is used to check if the Alt+Tab window is displayed
// On Windows 10 Alt+Tab window
//   className = "MultitaskingViewFrame" and Title = "Task Switching"
// I didn't check on other OS
// ----------------------------------------------------------------------------
BOOL CALLBACK EnumWindowsProcNAT(HWND hwnd, LPARAM lParam) {
    char className[256] = { 0 };
    GetClassNameA(hwnd, className, sizeof(className));

    if (EqualsIgnoreCase(className, "TaskSwitcherWnd") || EqualsIgnoreCase(className, "MultitaskingViewFrame")) {
        *reinterpret_cast<bool*>(lParam) = true;
        return FALSE; // Stop enumerating
    }

    return TRUE; // Continue enumerating
}

bool IsNativeATWDisplayed() {
    HWND hWnd = GetForegroundWindow();
    char className[256] = { 0 };
    GetClassNameA(hWnd, className, 256);
    return EqualsIgnoreCase(className, "TaskSwitcherWnd") || EqualsIgnoreCase(className, "MultitaskingViewFrame");
}

DWORD WINAPI ShowCustomToolTipThread(LPVOID pvParam) {
    AT_LOG_TRACE;

    // Get mouse coordinates
    POINT pt;
    GetCursorPos(&pt);

    ToolTipInfo* tti    = (ToolTipInfo*)pvParam;
    AT_LOG_INFO("tooltip: %s, duration: %d", WStrToUTF8(tti->ToolTipText).c_str(), tti->Duration);
    int duration        = tti->Duration;
    g_ToolInfo.lpszText = (LPWSTR)tti->ToolTipText.c_str();

    SendMessageW(g_hCustomToolTip, TTM_SETTOOLINFO  ,    0, (LPARAM)&g_ToolInfo);
    SendMessageW(g_hCustomToolTip, TTM_TRACKPOSITION,    0, (LPARAM)(DWORD)MAKELONG(pt.x + 0, pt.y + 0));
    SendMessageW(g_hCustomToolTip, TTM_TRACKACTIVATE, true, (LPARAM)(LPTOOLINFO)&g_ToolInfo);
    if (duration != -1) {
        AT_LOG_INFO("Start TIMER_CUSTOM_TOOLTIP timer");
        g_TooltipTimerId = SetTimer(nullptr, TIMER_CUSTOM_TOOLTIP, duration, HideCustomToolTip);
    }
    return 0;
}

void RestartAltTab() {
    // FIXME: Still this is not working :-(
#ifdef _DEBUG
    // Close/detach the console window of the current process
    // This is not needed in release build, as we are not using console window.
    // If you want to keep the console window in debug mode, comment the below lines.
    // If you want to keep the console window in debug mode, comment the below lines.
    FreeConsole(); // Detach the console window from the current process

    // bool result = FreeConsole();
    // if (!result) {
    //     AT_LOG_ERROR("Failed to free console!");
    // }
    //  Attach to an existing console (if any)
    // if (AttachConsole(ATTACH_PARENT_PROCESS) || AttachConsole(GetCurrentProcessId())) {
    //     // Close the console window
    //     FreeConsole();
    // }
#endif // _DEBUG

    if (g_GeneralSettings.IsProcessElevated) {
        // Relaunch AltTab with administrator privileges
        RelaunchAsAdminAndExit(true, false);
    } else {
        RestartApplication();
    }
}

int GetCurrentYear() {
    // Get the current time
    std::time_t currentTime = std::time(nullptr);

    // Convert the current time to a std::tm structure
    std::tm* localTime = std::localtime(&currentTime);

    // Extract the year from the tm structure
    int currentYear = localTime->tm_year + 1900;

    return currentYear;
}

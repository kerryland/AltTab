// AltTab.cpp : Defines the entry point for the application.
//
#include "PreCompile.h"

#include "AltTab.h"
#include "Logger.h"
#include "Utils.h"
#include "AltTabWindow.h"
#include "AltTabListView.h"
#include "AltTabTooltip.h"
#include "AltTabContextMenu.h"
#include "SettingsDialog.h"
#include "AutoStart.h"
#include "AltTabSettings.h"
#include "GlobalData.h"
#include "CheckForUpdates.h"
#include "version.h"
#include "Resource.h"
#include <CommCtrl.h>
#include <filesystem>
#include <format>
#include <shellapi.h>
#include <string>
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
HHOOK           g_KeyboardHook          = nullptr;             // Keyboard Hook
HWND            g_hMainWnd              = nullptr;             // AltTab main window handle
bool            g_IsAltTab              = false;               // Is Alt+Tab pressed
bool            g_IsAltCtrlTab          = false;               // Is Alt+Ctrl+Tab pressed
bool            g_IsAltBacktick         = false;               // Is Alt+Backtick pressed

GeneralSettings     g_GeneralSettings;                         // General settings
IsHungAppWindowFunc g_pfnIsHungAppWindow = nullptr;

UINT const WM_USER_ALTTAB_TRAYICON = WM_APP + 1;

HWND CreateMainWindow(HINSTANCE hInstance);
BOOL AddNotificationIcon(HWND hWndTrayIcon);
void CALLBACK CheckAltKeyIsReleased(HWND hWnd, UINT uMsg, UINT_PTR idEvent, DWORD dwTime);
void CALLBACK CheckForUpdatesTimerCB(HWND hWnd, UINT uMsg, UINT_PTR idEvent, DWORD dwTime);
namespace {
bool IsNativeATWDisplayed();
}

namespace {

    GeneralSettings GetGeneralSettingsImpl() {
        GeneralSettings settings;
        settings.IsProcessElevated = IsProcessElevated();
        settings.IsTaskElevated    = IsTaskRunWithHighestPrivileges();
        settings.IsRunAtStartup    = IsRunAtStartup();
        return settings;
    }
}

GeneralSettings GetGeneralSettings() {
    return GetGeneralSettingsImpl();
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

    g_hInstance = hInstance;

    // Per-monitor DPI awareness.
    if (!SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE)) {
        AT_LOG_ERROR("Failed to set DPI awareness!");
    }

#ifdef _AT_LOGGER
    CreateLogger();
    AT_LOG_INFO("-------------------------------------------------------------------------------");
    AT_LOG_INFO("CreateLogger done.");
#endif // _AT_LOGGER

    const std::wstring applicationPath = GetApplicationPath();
    AT_LOG_INFO("Application Info");
    AT_LOG_INFO("  - Path      : %ls", applicationPath.c_str());
    AT_LOG_INFO("  - Version   : %s", AT_FULL_VERSIONA);
    AT_LOG_INFO("  - ProcessID : %d", GetCurrentProcessId());

    InitGDIPlus();
    InitializeCOM();
    InitImageList();

    g_GeneralSettings = GetGeneralSettings();
    AT_LOG_INFO(
        "GeneralSettings: IsProcessElevated = %d, IsTaskElevated = %d, IsRunAtStartup = %d",
        g_GeneralSettings.IsProcessElevated,
        g_GeneralSettings.IsTaskElevated,
        g_GeneralSettings.IsRunAtStartup);

    CreateCustomToolTip();
    ShowCustomToolTip(L"Initializing AltTab...", 1000);

    // Load settings from AltTabSettings.ini file.
    try {
        ATLoadSettings();
    } catch (const std::exception& e) {
        (void)e; // only used by logging below
        AT_LOG_ERROR("Failed to load settings: %s", e.what());
    }

    // If we're relaunching for elevation, allow it.
    if (g_GeneralSettings.IsProcessElevated && !g_GeneralSettings.IsTaskElevated
        && wcsstr(GetCommandLineW(), L"--elevated")) {
        AT_LOG_INFO("Relaunching with elevated privileges (--elevated argument).");
        g_GeneralSettings.IsTaskElevated = true;
        if (g_GeneralSettings.IsRunAtStartup) {
            RunAtStartup(true, true);
        }
    }

    // Run-at-startup is managed through Task Scheduler; drop any legacy value.
    RemoveStartupFromRegistry();

    if (!RegisterAltTabWindow()) {
        AT_LOG_ERROR("Failed to register AltTab window class.");
        MessageBox(nullptr, L"Failed to register AltTab window class.", AT_PRODUCT_NAMEW, MB_OK | MB_ICONERROR);
        return 1;
    }

    // Hidden main window used for tray icon handling.
    g_hMainWnd = CreateMainWindow(hInstance);

    HINSTANCE hinstUser32 = LoadLibrary(L"user32.dll");
    if (hinstUser32) {
        g_pfnIsHungAppWindow = (IsHungAppWindowFunc)GetProcAddress(hinstUser32, "IsHungAppWindow");
    }

    if (g_Settings.SystemTrayIconEnabled && !AddNotificationIcon(g_hMainWnd)) {
        const std::wstring info = L"Failed to add AltTab tray icon.";
        AT_LOG_ERROR("Failed to add AltTab tray icon.");
        ShowCustomToolTip(info, 3000);

        Sleep(3000);
        RestartAltTab();
    }

    g_KeyboardHook = SetWindowsHookEx(WH_KEYBOARD_LL, LLKeyboardProc, hInstance, 0);

    HACCEL hAccelTable = LoadAccelerators(hInstance, MAKEINTRESOURCE(IDC_ALTTAB));

    // Check for updates.
    if (g_Settings.CheckForUpdatesOpt == L"Startup") {
        std::thread thr(CheckForUpdates, true);
        thr.detach();
    } else if (g_Settings.CheckForUpdatesOpt != L"Never") {
        // Check every hour; the callback decides based on the timestamp.
        SetTimer(g_hMainWnd, TIMER_CHECK_FOR_UPDATES, 3600000, CheckForUpdatesTimerCB);
    }

    MSG msg;
    while (GetMessage(&msg, nullptr, 0, 0)) {
        if (!TranslateAccelerator(msg.hwnd, hAccelTable, &msg)) {
            TranslateMessage(&msg);
            DispatchMessage(&msg);
        }
    }

    if (g_KeyboardHook) {
        UnhookWindowsHookEx(g_KeyboardHook);
        g_KeyboardHook = nullptr;
    }

    UninitializeCOM();
    ShutdownGDIPlus();

    return (int)msg.wParam;
}

// ----------------------------------------------------------------------------
// Main (tray) window procedure
// ----------------------------------------------------------------------------
LRESULT CALLBACK MainWndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam) {
    switch (message) {
    case WM_COMMAND: {
        switch (LOWORD(wParam)) {
        case ID_TRAYCONTEXTMENU_ABOUTALTTAB:
            DialogBox(g_hInstance, MAKEINTRESOURCE(IDD_ABOUTBOX), hWnd, ATAboutDlgProc);
            break;

        case ID_TRAYCONTEXTMENU_SETTINGS:
            DialogBoxW(g_hInstance, MAKEINTRESOURCE(IDD_SETTINGS), g_hMainWnd, ATSettingsDlgProc);
            break;

        case ID_TRAYCONTEXTMENU_EXIT:
            PostQuitMessage(0);
            break;
        }
    }
    break;

    case WM_USER_ALTTAB_TRAYICON: {
        switch (LOWORD(lParam)) {
        case WM_RBUTTONUP: {
            AT_LOG_INFO("WM_USER_ALTTAB_TRAYICON: WM_RBUTTONUP");
            POINT pt;
            GetCursorPos(&pt);
            ShowTrayContextMenu(hWnd, pt);
        }
        break;

        case WM_LBUTTONUP: {
            AT_LOG_INFO("WM_USER_ALTTAB_TRAYICON: WM_LBUTTONUP");
            // Alt+Ctrl+Tab behaves like Alt+Tab but stays open.
            g_IsAltCtrlTab = true;
            ShowAltTabWindow(g_hAltTabWnd, 0);
        }
        break;
        }
    }
    break;

    case WM_DESTROY:
        KillTimer(hWnd, TIMER_CHECK_FOR_UPDATES);
        break;

    default:
        return DefWindowProc(hWnd, message, wParam, lParam);
    }
    return 0;
}

// ----------------------------------------------------------------------------
// Hidden window for the tray icon
// ----------------------------------------------------------------------------
HWND CreateMainWindow(HINSTANCE hInstance) {
    HWND hwndFrgnd = GetForegroundWindow();
    DWORD idThreadAttachTo = hwndFrgnd ? GetWindowThreadProcessId(hwndFrgnd, nullptr) : 0;
    if (idThreadAttachTo) {
        AttachThreadInput(GetCurrentThreadId(), idThreadAttachTo, TRUE);
    }

    WNDCLASS wc      = {};
    wc.lpfnWndProc   = MainWndProc;
    wc.hInstance     = hInstance;
    wc.lpszClassName = L"__AltTab_MainWndCls__";
    wc.hIcon         = LoadIcon(g_hInstance, MAKEINTRESOURCE(IDI_ALTTAB));
    RegisterClass(&wc);

    return CreateWindowW(
        wc.lpszClassName,
        L"AltTabMainWindow",
        0,
        0,
        0,
        1,
        1,
        nullptr,
        nullptr,
        hInstance,
        nullptr);
}

BOOL AddNotificationIcon(HWND hWndTrayIcon) {
    NOTIFYICONDATA nid   = {};
    nid.cbSize           = sizeof(NOTIFYICONDATA);
    nid.hWnd             = hWndTrayIcon;
    nid.uID              = 1;
    nid.uFlags           = NIF_ICON | NIF_TIP | NIF_MESSAGE | NIF_SHOWTIP;
    nid.uCallbackMessage = WM_USER_ALTTAB_TRAYICON;
    nid.hIcon            = LoadIcon(g_hInstance, MAKEINTRESOURCE(IDI_ALTTAB));

    wcscpy_s(nid.szTip, AT_PRODUCT_NAMEW L" v" AT_VERSION_TEXTW);

    return Shell_NotifyIcon(NIM_ADD, &nid);
}

// ----------------------------------------------------------------------------
// Low level keyboard hook
//
// nCode < 0              -> must return CallNextHookEx.
// nCode >= 0, not handled-> call CallNextHookEx so other hooks keep working.
// handled               -> return non-zero to swallow the keystroke.
// ----------------------------------------------------------------------------
LRESULT CALLBACK LLKeyboardProc(int nCode, WPARAM wParam, LPARAM lParam) {
    if (nCode != HC_ACTION) {
        return CallNextHookEx(g_KeyboardHook, nCode, wParam, lParam);
    }

    auto* pKeyboard = reinterpret_cast<KBDLLHOOKSTRUCT*>(lParam);
    const DWORD vkCode = pKeyboard->vkCode;

    const bool isAltPressed  = GetAsyncKeyState(VK_MENU) & 0x8000;
    const bool isCtrlPressed = GetAsyncKeyState(VK_CONTROL) & 0x8000;

    // Remember the first Alt press (GetAsyncKeyState lags behind by one sample).
    const bool altKeyDown =
        isAltPressed || vkCode == VK_LMENU || vkCode == VK_RMENU || vkCode == VK_MENU;

    const bool windowVisible = (g_hAltTabWnd != nullptr);

    // Handle keys while the Alt key is down (Alt+Tab / Alt+Ctrl+Tab) or while
    // the AltTab window is visible (stick mode, e.g. Alt+Ctrl+Tab).
    if ((altKeyDown && (vkCode != VK_CONTROL && (!isCtrlPressed || g_Settings.HKAltCtrlTabEnabled))) || windowVisible) {
        const bool isShiftPressed = GetAsyncKeyState(VK_SHIFT) & 0x8000;
        const int direction = isShiftPressed ? -1 : 1;

        if (wParam == WM_KEYDOWN || wParam == WM_SYSKEYDOWN) {
            if (g_hAltTabWnd == nullptr) {
                // Starting the switch; don't show both AltTab and the native
                // Alt+Tab switcher.
                const bool nativeATWDisplayed = IsNativeATWDisplayed();

                const bool showAltTabWindow =
                    (g_Settings.HKAltTabEnabled && !isCtrlPressed)
                    || (g_Settings.HKAltCtrlTabEnabled && isCtrlPressed);

                if (showAltTabWindow && vkCode == VK_TAB) {
                    if (nativeATWDisplayed) {
                        return CallNextHookEx(g_KeyboardHook, nCode, wParam, lParam);
                    }

                    g_IsAltTab      = true;
                    g_IsAltBacktick = false;
                    AT_LOG_INFO(isShiftPressed ? "--------- Alt+Shift+Tab Pressed! ---------"
                                               : "--------- Alt+Tab Pressed! ---------");
                    ShowAltTabWindow(g_hAltTabWnd, direction);
                } else if (g_Settings.HKAltBacktickEnabled && vkCode == g_Settings.HKBacktickKey) {
                    g_IsAltTab      = false;
                    g_IsAltBacktick = true;
                    AT_LOG_INFO(isShiftPressed ? "--------- Alt+Shift+Backtick Pressed! ---------"
                                               : "--------- Alt+Backtick Pressed! ---------");
                    ShowAltTabWindow(g_hAltTabWnd, direction);
                } else {
                    return CallNextHookEx(g_KeyboardHook, nCode, wParam, lParam);
                }

                // Close the window when Alt is released (unless Alt+Ctrl+Tab).
                if (g_IsAltTab || g_IsAltBacktick) {
                    if (!g_Settings.HKAltCtrlTabEnabled || !isCtrlPressed) {
                        SetTimer(g_hMainWnd, TIMER_CHECK_ALT_KEYUP, 50, CheckAltKeyIsReleased);
                    } else {
                        g_IsAltCtrlTab = true;
                    }
                }
                return TRUE;
            }

            // ---- The AltTab window is already visible. ----
            if (vkCode == VK_TAB) {
                ShowAltTabWindow(g_hAltTabWnd, direction);
                return TRUE;
            }

            // Swallow Alt+Escape so the system doesn't switch windows.
            if (vkCode == VK_ESCAPE) {
                AT_LOG_INFO("Escape Pressed!");
                if (g_hContextMenu != nullptr) {
                    DestroyContextMenu();
                }
                DestroyAltTabWindow();
                return TRUE;
            }

            if (vkCode == VK_APPS && g_SelectedIndex != -1) {
                AT_LOG_INFO("Apps Pressed!");
                ShowContextMenuAtItemCenter();
                return TRUE;
            }

            // Forward the backtick to the ListView so it can move between
            // similar-process windows, and keep the key from reaching other apps.
            if (vkCode == g_Settings.HKBacktickKey) {
                PostMessage(g_hListView, WM_KEYDOWN, vkCode, 0);
                return TRUE;
            }

            // Prevent PowerToys' Alt+Space while the AltTab window is open.
            if (vkCode == VK_SPACE) {
                PostMessageW(g_hSearchString, WM_KEYDOWN, vkCode, 0);
                return TRUE;
            }

            // Alt+F4 must not close the AltTab window.
            if (vkCode == VK_F4) {
                return TRUE;
            }
        }
    }

    return CallNextHookEx(g_KeyboardHook, nCode, wParam, lParam);
}

// ----------------------------------------------------------------------------
// Timer callbacks
// ----------------------------------------------------------------------------
void CALLBACK CheckForUpdatesTimerCB(HWND /*hWnd*/, UINT /*uMsg*/, UINT_PTR /*idEvent*/, DWORD /*dwTime*/) {
    AT_LOG_TRACE;
    const std::wstring frequency = g_Settings.CheckForUpdatesOpt;
    const auto lastCheckTimestamp = ReadLastCheckForUpdatesTS();
    const auto currentTimeStamp = std::chrono::system_clock::now();
    const auto timeDiff = currentTimeStamp - lastCheckTimestamp;

    if ((frequency == L"Daily" && timeDiff >= std::chrono::hours(24))
        || (frequency == L"Weekly" && timeDiff >= std::chrono::hours(24 * 7))) {
        CheckForUpdates(true);
        WriteCheckForUpdatesTS(currentTimeStamp);
    } else {
        AT_LOG_INFO("Update check not required at this time.");
    }
}

void CALLBACK CheckAltKeyIsReleased(HWND /*hWnd*/, UINT /*uMsg*/, UINT_PTR /*idEvent*/, DWORD /*dwTime*/) {
    const bool isAltPressed = GetAsyncKeyState(VK_MENU) & 0x8000;
    if (g_hAltTabWnd && !isAltPressed) {
        AT_LOG_INFO("--------- Alt key released! ---------");
        DestroyAltTabWindow(true);
    }
}

// ----------------------------------------------------------------------------
// Alt+Tab window detection (avoid double switchers)
// ----------------------------------------------------------------------------
namespace {

// On Windows 10/11 the native Alt+Tab UI uses these class names.
bool IsNativeATWDisplayed() {
    HWND hWnd = GetForegroundWindow();
    char className[256] = {};
    GetClassNameA(hWnd, className, 256);
    return EqualsIgnoreCase(className, "TaskSwitcherWnd") || EqualsIgnoreCase(className, "MultitaskingViewFrame");
}

} // namespace

void RestartAltTab() {
    if (g_GeneralSettings.IsProcessElevated) {
        RelaunchAsAdminAndExit(true, false);
    } else {
        RestartApplication();
    }
}
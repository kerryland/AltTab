// AltTabWindow.cpp : AltTab window class, message handling and lifecycle.
//

#include "PreCompile.h"
#include "AltTabWindow.h"
#include "AltTabListView.h"
#include "AltTabContextMenu.h"
#include "AltTabTooltip.h"
#include "SettingsDialog.h"
#include "ProcessExplorer.h"
#include "GlobalData.h"
#include "Logger.h"
#include "Utils.h"
#include "AltTabSettings.h"
#include "version.h"
#include "Resource.h"
#include <CommCtrl.h>
#include <filesystem>
#include <shellapi.h>
#include <windowsx.h>

// ----------------------------------------------------------------------------
// Global Variables:
// ----------------------------------------------------------------------------
HWND     g_hAltTabWnd           = nullptr;
bool     g_hAltTabIsBeingClosed = false;                // Is AltTab window being closed
bool     g_bIgnoreWM_ACTIVATE   = false;                // Ignore WM_ACTIVATE with WA_INACTIVE
DWORD    g_idThreadAttachTo     = 0;                    // Thread whose input we attached to

// Window procedure functions
INT_PTR CALLBACK AltTabWindowProc(HWND, UINT, WPARAM, LPARAM);

static void    ATW_OnActivate   (HWND hwnd, UINT state, HWND hwndActDeact, BOOL fMinimized);
static void    ATW_OnClose      (HWND hwnd);
static void    ATW_OnCommand    (HWND hwnd, int id, HWND hwndCtl, UINT codeNotify);
static void    ATW_OnContextMenu(HWND hwnd, HWND hwndContext, UINT xPos, UINT yPos);
static BOOL    ATW_OnCreate     (HWND hWnd, LPCREATESTRUCT lpCreateStruct);
static LRESULT ATW_OnCtlColorEdit  (HWND hWnd, HDC hDC, HWND hCtl, UINT type);
static LRESULT ATW_OnCtlColorStatic(HWND hWnd, HDC hDC, HWND hCtl, UINT type);
static void    ATW_OnDestroy    (HWND hwnd);
static void    ATW_OnDrawItem   (HWND hwnd, const DRAWITEMSTRUCT* lpDrawItem);
static void    ATW_OnKeyDown    (HWND hwnd, UINT vk, BOOL fDown, int cRepeat, UINT flags);
static BOOL    ATW_OnNotify     (HWND hwnd, int idFrom, NMHDR* pnmhdr);
static void    ATW_OnSysCommand (HWND hwnd, UINT cmd, int x, int y);
static void    ATW_OnTimer      (HWND hwnd, UINT id);

bool RegisterAltTabWindow() {
    AT_LOG_TRACE;

    WNDCLASS wc      = {};
    wc.lpfnWndProc   = AltTabWindowProc;
    wc.lpszClassName = CLASS_NAME;
    wc.hInstance     = g_hInstance;

    if (!RegisterClass(&wc)) {
        AT_LOG_ERROR("Failed to register AltTab Window class!");
        LogLastErrorInfo();
        return false;
    }
    return true;
}

HWND CreateAltTabWindow() {
    AT_LOG_TRACE;

    const DWORD exStyle = WS_EX_TOOLWINDOW | WS_EX_TOPMOST;
    const DWORD style   = WS_POPUP | WS_BORDER;

    HWND hWnd = CreateWindowExW(
        exStyle,
        CLASS_NAME,
        WINDOW_NAME,
        style,
        0,
        0,
        0,
        0,
        g_hMainWnd,
        nullptr,
        g_hInstance,
        nullptr);

    if (hWnd == nullptr) {
        AT_LOG_ERROR("Failed to create AltTab Window!");
        return nullptr;
    }

    SetForegroundWindow(hWnd);
    SetWindowPos(hWnd, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_SHOWWINDOW);
    ShowWindow(hWnd, SW_SHOWNORMAL);
    UpdateWindow(hWnd);
    BringWindowToTop(hWnd);

    return hWnd;
}

HWND ShowAltTabWindow(HWND& hAltTabWnd, int direction) {
    if (hAltTabWnd == nullptr) {
        // Required for the AltTab window to become the foreground window.
        const HWND hForegroundWnd = GetForegroundWindow();
        if (!IsHungAppWindowEx(hForegroundWnd)) {
            g_idThreadAttachTo = hForegroundWnd ? GetWindowThreadProcessId(hForegroundWnd, nullptr) : 0;
            if (g_idThreadAttachTo) {
                AttachThreadInput(GetCurrentThreadId(), g_idThreadAttachTo, TRUE);
            }
        }

        // Alt+Backtick must switch among the windows similar to the one that
        // was focused when the hotkey was pressed. Capture it here, before the
        // AltTab window steals the foreground, so the enumeration below filters
        // on it instead of whichever window EnumWindows happens to see first.
        if (g_IsAltBacktick) {
            AltTabWindowData src = {};
            if (hForegroundWnd != nullptr) {
                src.hWnd        = hForegroundWnd;
                src.ProcessName = GetProcessNameFromWindow(hForegroundWnd);
            }
            AT_LOG_INFO(
                "Alt+Backtick source window: %#x, process: [%ls]",
                src.hWnd,
                src.ProcessName.c_str());
            g_AltBacktickWndInfo = src;
        }

        hAltTabWnd = CreateAltTabWindow();
        SetForegroundWindow(hAltTabWnd);
    }

    // Always give focus to the search string edit control.
    SetFocus(g_hSearchString);

    // Move to next / previous item based on the direction.
    const int selectedInd = (int)SendMessageW(g_hListView, LVM_GETNEXTITEM, (WPARAM)-1, LVNI_SELECTED);
    if (selectedInd == -1)
        return hAltTabWnd;

    const int N = (int)g_AltTabWindows.size();
    if (N == 0) {
        AT_LOG_ERROR("No AltTab windows available!");
        return hAltTabWnd;
    }

    const int nextInd = (selectedInd + N + direction) % N;
    ATWListViewSelectItem(nextInd);

    TRACKMOUSEEVENT tme = {};
    tme.cbSize    = sizeof(tme);
    tme.dwFlags   = TME_LEAVE;
    tme.hwndTrack = hAltTabWnd;
    TrackMouseEvent(&tme);

    return hAltTabWnd;
}

void SetAltTabActiveWindow() {
    SetForegroundWindow(g_hAltTabWnd);
    SetActiveWindow(g_hAltTabWnd);
    SetFocus(g_hSearchString);
}

// ----------------------------------------------------------------------------
// AltTab window procedure
// ----------------------------------------------------------------------------

INT_PTR CALLBACK AltTabWindowProc(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
    switch (uMsg) {
        HANDLE_MSG(hWnd, WM_ACTIVATE      , ATW_OnActivate      );
        HANDLE_MSG(hWnd, WM_CLOSE         , ATW_OnClose         );
        HANDLE_MSG(hWnd, WM_COMMAND       , ATW_OnCommand       );
        HANDLE_MSG(hWnd, WM_CONTEXTMENU   , ATW_OnContextMenu   );
        HANDLE_MSG(hWnd, WM_CREATE        , ATW_OnCreate        );
        HANDLE_MSG(hWnd, WM_CTLCOLOREDIT  , ATW_OnCtlColorEdit  );
        HANDLE_MSG(hWnd, WM_CTLCOLORSTATIC, ATW_OnCtlColorStatic);
        HANDLE_MSG(hWnd, WM_DESTROY       , ATW_OnDestroy       );
        HANDLE_MSG(hWnd, WM_DRAWITEM      , ATW_OnDrawItem      );
        HANDLE_MSG(hWnd, WM_KEYDOWN       , ATW_OnKeyDown       );
        HANDLE_MSG(hWnd, WM_NOTIFY        , ATW_OnNotify        );
        HANDLE_MSG(hWnd, WM_SYSCOMMAND    , ATW_OnSysCommand    );
        HANDLE_MSG(hWnd, WM_TIMER         , ATW_OnTimer         );

        case WM_SYSCHAR:
            // Don't let DefWindowProc play the menu beep for Alt+key combos
            // that reach us (e.g. an Alt+Backtick repeat a hook sample missed);
            // the AltTab window owns all keys while it is open.
            return 0;

        default:
            return DefWindowProc(hWnd, uMsg, wParam, lParam);
    }
}

static BOOL ATW_OnCreate(HWND hWnd, LPCREATESTRUCT /*lpCreateStruct*/) {
    AT_LOG_TRACE;
    g_hAltTabWnd = hWnd;
    AT_LOG_INFO("AltTab Window Handle: [%#08X]", hWnd);

    InitListView(hWnd);

    return TRUE;
}

static void ATW_OnActivate(HWND /*hwnd*/, UINT state, HWND /*hwndActDeact*/, BOOL /*fMinimized*/) {
    AT_LOG_TRACE;
    AT_LOG_INFO("WM_ACTIVATE: g_bIgnoreWM_ACTIVATE: %d", g_bIgnoreWM_ACTIVATE);
    if (state == WA_INACTIVE && !g_bIgnoreWM_ACTIVATE) {
        AT_LOG_INFO("WM_ACTIVATE: The application is becoming inactive, close the window");
        DestroyAltTabWindow();
    }
}

static void ATW_OnClose(HWND /*hwnd*/) {
    AT_LOG_TRACE;
    PostQuitMessage(0);
}

static void ATW_OnCommand(HWND /*hwnd*/, int id, HWND /*hwndCtl*/, UINT /*codeNotify*/) {
    AT_LOG_TRACE;
    if (id == IDOK || id == IDCANCEL) {
        PostQuitMessage(0);
    }
}

static void ATW_OnContextMenu(HWND hwnd, HWND /*hwndContext*/, UINT xPos, UINT yPos) {
    AT_LOG_TRACE;
    ShowContextMenu(hwnd, { (LONG)xPos, (LONG)yPos });
}

static void ATW_OnSysCommand(HWND hwnd, UINT cmd, int x, int y) {
    AT_LOG_TRACE;
    if (cmd == SC_KEYMENU && (x == -1 && y == -1)) {
        // Alt+Space pressed; show the system menu manually.
        POINT cursorPos;
        GetCursorPos(&cursorPos);
        TrackPopupMenu(GetSystemMenu(hwnd, FALSE), 0, cursorPos.x, cursorPos.y, 0, hwnd, nullptr);
    }
}

static LRESULT ATW_OnCtlColorEdit(HWND hWnd, HDC hDC, HWND hCtl, UINT /*type*/) {
    AT_LOG_TRACE;
    if (hCtl == g_hSearchString) {
        return AltTabSearchControlColor(hDC, true);
    }
    return DefWindowProc(hWnd, WM_CTLCOLOREDIT, (WPARAM)hDC, (LPARAM)hCtl);
}

static LRESULT ATW_OnCtlColorStatic(HWND hWnd, HDC hDC, HWND hCtl, UINT /*type*/) {
    AT_LOG_TRACE;
    if (hCtl == g_hSearchString) {
        return AltTabSearchControlColor(hDC, false);
    }
    return DefWindowProc(hWnd, WM_CTLCOLORSTATIC, (WPARAM)hDC, (LPARAM)hCtl);
}

static void ATW_OnKeyDown(HWND /*hwnd*/, UINT vk, BOOL /*fDown*/, int /*cRepeat*/, UINT /*flags*/) {
    AT_LOG_TRACE;
    if (vk == VK_ESCAPE) {
        AT_LOG_INFO("WM_KEYDOWN: VK_ESCAPE");
        DestroyAltTabWindow();
    }
}

static BOOL ATW_OnNotify(HWND hwnd, int /*idFrom*/, NMHDR* pnmhdr) {
    AltTabWindow_OnNotify(hwnd, pnmhdr);
    return FALSE;
}

static void ATW_OnDrawItem(HWND /*hwnd*/, const DRAWITEMSTRUCT* lpDrawItem) {
    // Forward owner-draw to the ListView subclass.
    if (lpDrawItem->CtlType == ODT_LISTVIEW) {
        SendMessageW(lpDrawItem->hwndItem, WM_DRAWITEM, 0, (LPARAM)lpDrawItem);
    }
}

static void ATW_OnTimer(HWND /*hwnd*/, UINT /*id*/) {
#ifdef _AT_LOGGER
    if (!g_SearchString.empty())
        AT_LOG_DEBUG("g_SearchString: [%ls]", g_SearchString.c_str());
#endif

    const std::vector<AltTabWindowData> altTabWindows = GetAltTabWindows();

    bool doRefresh = altTabWindows.size() != g_AltTabWindows.size();
    if (!doRefresh) {
        for (size_t i = 0; i < altTabWindows.size(); ++i) {
            if (altTabWindows[i] != g_AltTabWindows[i]) {
                doRefresh = true;
                break;
            }
        }
    }

    if (doRefresh) {
        RefreshAltTabWindow();
    }
}

static void ATW_OnDestroy(HWND hwnd) {
    AT_LOG_TRACE;
    KillTimer(hwnd, TIMER_WINDOW_COUNT);
}

// ----------------------------------------------------------------------------
// AltTab window lifecycle
// ----------------------------------------------------------------------------

void DestroyAltTabWindow(const bool activate) {
    if (g_hAltTabWnd == nullptr || g_hAltTabIsBeingClosed) {
        return;
    }

    // Close the context menu first (Escape pressed while a menu is open).
    if (g_hContextMenu != nullptr) {
        DestroyContextMenu();
    }

    AT_LOG_INFO("DestroyAltTabWindow: activate = %d", activate);

    g_hAltTabIsBeingClosed = true;

    HideCustomToolTip();
    KillTimer(g_hMainWnd, TIMER_CHECK_ALT_KEYUP);

    if (g_idThreadAttachTo) {
        AttachThreadInput(GetCurrentThreadId(), g_idThreadAttachTo, FALSE);
        g_idThreadAttachTo = 0;
    }

    HWND hWnd = nullptr;
    if (activate) {
        const int selectedInd = (int)SendMessageW(g_hListView, LVM_GETNEXTITEM, (WPARAM)-1, LVNI_SELECTED);
        if (selectedInd != -1 && selectedInd < (int)g_AltTabWindows.size()) {
            hWnd = g_AltTabWindows[selectedInd].hWnd;
            AT_LOG_INFO("hWnd = [%#x], title = [%s]", hWnd, GetWindowTitleExA(hWnd).c_str());
        }
    }

    HWND hLostWnd = g_hAltTabWnd;
    g_hAltTabWnd = nullptr;
    DestroyWindow(hLostWnd);

    if (activate && hWnd && !IsHungAppWindowEx(hWnd)) {
        ActivateWindow(hWnd);
    }

    // Cleanup state.
    AltTabListViewShutdown();

    g_IsAltTab               = false;
    g_IsAltCtrlTab           = false;
    g_IsAltBacktick          = false;
    g_SelectedIndex          = -1;
    g_AltBacktickWndInfo     = {};   // re-captured on the next Alt+Backtick
    g_hAltTabIsBeingClosed   = false;
}

// ----------------------------------------------------------------------------
// Activate a window of the given handle
// ----------------------------------------------------------------------------

void ActivateWindow(HWND hTargetWnd) {
    AT_LOG_TRACE;

    HWND hForegroundWnd = GetForegroundWindow();
    if (hTargetWnd == hForegroundWnd) {
        return;
    }

    if (IsIconic(hTargetWnd)) {
        PostMessage(hTargetWnd, WM_SYSCOMMAND, SC_RESTORE, 0);
        return;
    }

    if (SetForegroundWindow(hTargetWnd)) {
        SetActiveWindow(hTargetWnd);
        return;
    }

    // Failed (usually because the target window is elevated). Try to bring it
    // to the top with the standard tricks.
    AT_LOG_ERROR("SetForegroundWindow(hWnd) failed!");

    ShowWindow(hTargetWnd, SW_SHOW);
    BringWindowToTop(hTargetWnd);
    SetActiveWindow(hTargetWnd);

    DWORD idForeground = hForegroundWnd ? GetWindowThreadProcessId(hForegroundWnd, nullptr) : 0;
    DWORD idTarget     = GetWindowThreadProcessId(hTargetWnd, nullptr);

    if (idForeground && !IsHungAppWindowEx(hForegroundWnd))
        AttachThreadInput(idForeground, idTarget, TRUE);

    if (!SetForegroundWindow(hTargetWnd)) {
        // Last resort: send a harmless Alt-key toggle to satisfy the foreground
        // lock, then retry.
        INPUT inp[4] = {};
        inp[0].type       = inp[1].type       = inp[2].type   = inp[3].type   = INPUT_KEYBOARD;
        inp[0].ki.wVk     = inp[1].ki.wVk     = inp[2].ki.wVk = inp[3].ki.wVk = VK_MENU;
        inp[0].ki.dwFlags = inp[2].ki.dwFlags = KEYEVENTF_EXTENDEDKEY;
        inp[1].ki.dwFlags = inp[3].ki.dwFlags = KEYEVENTF_EXTENDEDKEY | KEYEVENTF_KEYUP;
        SendInput(4, inp, sizeof(INPUT));

        SetForegroundWindow(hTargetWnd);
    }

    if (idForeground && !IsHungAppWindowEx(hForegroundWnd))
        AttachThreadInput(idForeground, idTarget, FALSE);

    SetActiveWindow(hTargetWnd);
}

BOOL IsHungAppWindowEx(HWND hwnd) {
    if (g_pfnIsHungAppWindow && g_pfnIsHungAppWindow(hwnd)) {
        AT_LOG_INFO("IsHungWnd: [%s]", GetWindowTitleExA(hwnd).c_str());
        return TRUE;
    }

    LRESULT lResult = SendMessageTimeoutW(hwnd, WM_NULL, 0, 0, SMTO_ABORTIFHUNG, 249, nullptr);
    if (lResult)
        return FALSE;

    const DWORD dwErr = GetLastError();
    return (dwErr == 0 || dwErr == 1460);
}

// ----------------------------------------------------------------------------
// Helpers
// ----------------------------------------------------------------------------

void ShowHelpWindow() {
    ATShellExecuteEx(L"AltTab.chm");
}

void ShowReadMeWindow() {
    ATShellExecuteEx(L"ReadMe.txt");
}

void ShowReleaseNotesWindow() {
    ATShellExecuteEx(L"ReleaseNotes.txt");
}

void ATShellExecuteEx(const std::wstring& fileName) {
    std::filesystem::path filePath = GetAppDirPath();
    filePath.append(fileName);

    HINSTANCE hInstance = ShellExecuteW(nullptr, L"open", filePath.wstring().c_str(), nullptr, nullptr, SW_SHOWNORMAL);

    if ((INT_PTR)hInstance > 32) {
        AT_LOG_INFO("File opened successfully!");
    } else {
        AT_LOG_ERROR("Failed to open file!");
        LogLastErrorInfo();
    }
}

std::wstring GetAppDirPath() {
    wchar_t szPath[MAX_PATH] = {};
    GetModuleFileNameW(g_hInstance, szPath, MAX_PATH);
    std::filesystem::path dirPath = szPath;
    return dirPath.parent_path().wstring();
}

void LogLastErrorInfo() {
    const DWORD errorCode = GetLastError();

    LPVOID errorMessage = nullptr;
    FormatMessageW(
        FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM,
        nullptr,
        errorCode,
        0,
        reinterpret_cast<LPWSTR>(&errorMessage),
        0,
        nullptr);

    const std::wstring ret = errorMessage ? reinterpret_cast<LPCWSTR>(errorMessage) : L"";
    AT_LOG_ERROR("  Error Code   : %d", errorCode);
    AT_LOG_ERROR("  Error Message: %s", WStrToUTF8(ret).c_str());

    if (errorMessage) {
        LocalFree(errorMessage);
    }
}

int ATMessageBoxW(HWND hWnd, LPCWSTR lpText, LPCWSTR lpCaption, UINT uType) {
    // Prevent the AltTab window from closing when the message box is shown.
    g_bIgnoreWM_ACTIVATE = true;
    const int result = MessageBoxW(hWnd, lpText, lpCaption, uType);
    g_bIgnoreWM_ACTIVATE = false;
    return result;
}

// ----------------------------------------------------------------------------
// About box
// ----------------------------------------------------------------------------

INT_PTR CALLBACK ATAboutDlgProc(HWND hDlg, UINT message, WPARAM wParam, LPARAM lParam) {
    UNREFERENCED_PARAMETER(lParam);
    switch (message) {
    case WM_INITDIALOG: {
        HICON hIcon = LoadIcon(g_hInstance, MAKEINTRESOURCE(IDI_ALTTAB));
        SendMessageW(hDlg, WM_SETICON, ICON_SMALL, (LPARAM)hIcon);
        SendMessageW(hDlg, WM_SETICON, ICON_BIG, (LPARAM)hIcon);

        // Center the dialog.
        const int screenWidth  = GetSystemMetrics(SM_CXSCREEN);
        const int screenHeight = GetSystemMetrics(SM_CYSCREEN);
        RECT dlgRect;
        GetWindowRect(hDlg, &dlgRect);

        const int posX = (screenWidth - (dlgRect.right - dlgRect.left)) / 2;
        const int posY = (screenHeight - (dlgRect.bottom - dlgRect.top)) / 2;
        SetWindowPos(hDlg, HWND_TOP, posX, posY, 0, 0, SWP_NOSIZE);

        // Show in the taskbar.
        SetWindowLong(hDlg, GWL_EXSTYLE, GetWindowLong(hDlg, GWL_EXSTYLE) | WS_EX_APPWINDOW);

        const std::wstring productInfo =
            std::format(L"<a href=\"{}\">{}</a> v{}", AT_PRODUCT_PAGE, AT_PRODUCT_NAMEW, AT_VERSION_TEXTW);
        const std::wstring copyright =
            std::format(L"Copyright (c) {} <a href=\"{}\">{}</a>", AT_PRODUCT_YEARW, AT_PRODUCT_PAGE, AT_AUTHOR_NAME);

        SetDlgItemTextW(hDlg, IDC_SYSLINK_ABOUT_PRODUCT_NAME, productInfo.c_str());
        SetDlgItemTextW(hDlg, IDC_SYSLINK_ABOUT_COPYRIGHT, copyright.c_str());
    }
    return (INT_PTR)TRUE;

    case WM_COMMAND:
        if (LOWORD(wParam) == IDOK || LOWORD(wParam) == IDCANCEL) {
            EndDialog(hDlg, LOWORD(wParam));
            return (INT_PTR)TRUE;
        }
        break;

    case WM_NOTIFY: {
        NMHDR* pnmh = (NMHDR*)lParam;
        if (wParam == IDC_SYSLINK_ABOUT_PRODUCT_NAME && pnmh->code == NM_CLICK) {
            ShellExecute(nullptr, L"open", AT_PRODUCT_PAGE, nullptr, nullptr, SW_SHOWNORMAL);
        } else if (wParam == IDC_SYSLINK_ABOUT_COPYRIGHT && pnmh->code == NM_CLICK) {
            ShellExecute(nullptr, L"open", AT_AUTHOR_PAGE, nullptr, nullptr, SW_SHOWNORMAL);
        }
    }
    break;
    }
    return (INT_PTR)FALSE;
}
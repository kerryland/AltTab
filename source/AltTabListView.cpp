#include "PreCompile.h"
#include "AltTabListView.h"
#include "AltTabWindow.h"
#include "AltTabSettings.h"
#include "AltTabTooltip.h"
#include "AltTabContextMenu.h"
#include "ProcessExplorer.h"
#include "SettingsDialog.h"
#include "WindowPreview.h"
#include "GlobalData.h"
#include "Logger.h"
#include "Utils.h"
#include "Resource.h"
#include <algorithm>
#include <atomic>
#include <comdef.h>
#include <format>
#include <gdiplus.h>
#include <memory>
#include <mutex>
#include <set>
#include <thread>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace {

// ----------------------------------------------------------------------------
// Asynchronous window enumeration
//
// Enumerating windows probes every process on the desktop (WM_GETTEXT /
// WM_GETICON plus PE version reads) and is far too slow to run inside the
// low-level keyboard hook: Windows silently removes a hook that does not
// return within ~1 s (LowLevelHooksTimeout), which historically let Alt+Tab
// fall back to the native switcher under load. The enumeration therefore runs
// on a detached worker thread; the hook only pops the window over, and the UI
// thread consumes the finished list via WM_USER_ALTTAB_ENUM_DONE.
// ----------------------------------------------------------------------------
struct EnumResult {
    DWORD                         generation = 0;
    std::vector<AltTabWindowData> windows;
};

// Worker publishes here; the UI thread consumes it on the notification.
std::mutex                   g_enumSlotMutex;
std::shared_ptr<EnumResult>  g_enumSlot;

// UI-thread bookkeeping; never touched by workers.
DWORD g_enumGeneration          = 0;    // bumped for every worker we start
bool  g_enumInFlight            = false; // a worker is currently enumerating
bool  g_enumPending             = false; // a refresh was requested while busy
bool  g_altTabWindowInitialized = false; // the first population was applied
int   g_initialSwitchDirection  = 1;     // direction of the opening hotkey

// The enumeration runs on at most one worker at a time; keep its handle so it
// can be joined before process teardown instead of racing the CRT.
std::thread       g_enumWorker;
std::atomic<bool> g_enumShutdown{false};

// ----------------------------------------------------------------------------
// Module globals
// ----------------------------------------------------------------------------
HFONT      g_hSSFont              = nullptr;
HFONT      g_hLVFont              = nullptr;
int        g_MouseHoverIndex      = -1;
HBRUSH     g_hSSBackgroundBrush   = nullptr;
COLORREF   g_cachedSSBgColor      = 0xFFFFFFFF;

RECT       g_rcBtnClose;
bool       g_IsMouseOverCloseButton = false;

constexpr int COL_ICON_WIDTH    = 36;
constexpr int COL_PROCNAME_WIDTH = 180;
constexpr int g_nIconSize       = 32;

// Cap the wait when asking another window to close, so a slow application
// cannot stall the AltTab UI thread.
constexpr DWORD kCloseReplyTimeoutMs = 1000;

#define FONT_POINT(hdc, p) (-MulDiv(p, GetDeviceCaps(hdc, LOGPIXELSY), 72))

HFONT CreateFontEx(HDC hdc, const std::wstring& fontName, int fontSize, const std::wstring& fontStyle) {
    const std::unordered_map<std::wstring, int> fontStyleMap = {
        { L"normal"     , FW_NORMAL },
        { L"italic"     , FW_NORMAL },
        { L"bold"       , FW_BOLD   },
        { L"bold italic", FW_BOLD   },
    };

    const int fStyle = fontStyleMap.count(fontStyle) ? fontStyleMap.at(fontStyle) : FW_NORMAL;
    const BOOL bItalic = fontStyle.find(L"italic") != std::wstring::npos;

    return CreateFontW(
        FONT_POINT(hdc, fontSize),
        0,
        0,
        0,
        fStyle,
        bItalic,
        FALSE,
        FALSE,
        DEFAULT_CHARSET,
        OUT_DEFAULT_PRECIS,
        CLIP_DEFAULT_PRECIS,
        DEFAULT_QUALITY,
        DEFAULT_PITCH | FF_SWISS,
        fontName.c_str());
}

void SetListViewCustomColors(HWND hListView, COLORREF backgroundColor, COLORREF textColor) {
    SendMessageW(hListView, LVM_SETBKCOLOR, 0, (LPARAM)backgroundColor);
    SendMessageW(hListView, LVM_SETTEXTBKCOLOR, 0, (LPARAM)backgroundColor);
    SendMessageW(hListView, LVM_SETTEXTCOLOR, 0, (LPARAM)textColor);
}

void CustomizeListView(HWND hListView, int dpi) {
    const DWORD dwExStyle = LVS_EX_FULLROWSELECT | LVS_EX_DOUBLEBUFFER;
    ListView_SetExtendedListViewStyle(hListView, dwExStyle);

    const int colTitleWidth = g_Settings.WindowWidth - COL_ICON_WIDTH;

    LVCOLUMN lvCol = {};
    lvCol.mask    = LVCF_TEXT | LVCF_WIDTH;
    lvCol.pszText = (LPWSTR)L"#";
    lvCol.cx      = ScaleValueForDPI(COL_ICON_WIDTH, dpi);
    ListView_InsertColumn(hListView, 0, &lvCol);

    if (g_Settings.ShowColProcessName) {
        lvCol.pszText = (LPWSTR)L"Window Title";
        lvCol.cx      = colTitleWidth - COL_PROCNAME_WIDTH;
        ListView_InsertColumn(hListView, 1, &lvCol);

        lvCol.pszText = (LPWSTR)L"Process Name";
        lvCol.cx      = COL_PROCNAME_WIDTH - 2;
        ListView_InsertColumn(hListView, 2, &lvCol);
    } else {
        lvCol.pszText = (LPWSTR)L"Window Title";
        lvCol.cx      = colTitleWidth - 2;
        ListView_InsertColumn(hListView, 1, &lvCol);
    }
}

int GetColProcessNameWidth() {
    return g_Settings.ShowColProcessName ? COL_PROCNAME_WIDTH : 0;
}

void WindowResizeAndPosition(HWND hWnd, int wndWidth, int wndHeight) {
    const int screenWidth  = GetSystemMetrics(SM_CXSCREEN);
    const int screenHeight = GetSystemMetrics(SM_CYSCREEN);

    const int xPos = (screenWidth - wndWidth) / 2;
    const int yPos = (screenHeight - wndHeight) / 2;

    SetWindowPos(hWnd, HWND_TOP, xPos, yPos, wndWidth, wndHeight, SWP_NOSIZE | SWP_SHOWWINDOW);
}

void AddListViewItem(HWND hListView, int index, const AltTabWindowData& windowData) {
    LVITEM lvItem   = {};
    lvItem.mask     = LVIF_TEXT | LVIF_IMAGE | LVIF_PARAM;
    lvItem.iItem    = index;
    lvItem.iSubItem = 0;
    lvItem.iImage   = index;
    lvItem.lParam   = (LPARAM)(&windowData);

    ListView_InsertItem(hListView, &lvItem);
    ListView_SetItemText(hListView, index, 1, const_cast<wchar_t*>(windowData.Title.c_str()));
    ListView_SetItemText(hListView, index, 2, const_cast<wchar_t*>(windowData.ProcessName.c_str()));
}

/*!
 * \brief Handle keys common to both the search-string and ListView subclasses.
 * \return true if the key was handled.
 */
bool HandleCommonAltTabKeys(HWND /*hwnd*/, UINT vkCode) {
    const bool isShiftPressed = GetAsyncKeyState(VK_SHIFT) & 0x8000;

    if (vkCode == VK_ESCAPE) {
        AT_LOG_INFO("VK_ESCAPE");
        DestroyAltTabWindow();
        return true;
    }

    if (vkCode == VK_DOWN) {
        ATWListViewSelectNextItem();
        return true;
    }

    if (vkCode == VK_UP) {
        ATWListViewSelectPrevItem();
        return true;
    }

    if (vkCode == VK_HOME || vkCode == VK_PRIOR) {
        if (!g_AltTabWindows.empty()) {
            ATWListViewSelectItem(0);
        }
        return true;
    }

    if (vkCode == VK_END || vkCode == VK_NEXT) {
        if (!g_AltTabWindows.empty()) {
            ATWListViewSelectItem((int)g_AltTabWindows.size() - 1);
        }
        return true;
    }

    if (vkCode == VK_DELETE) {
        const int ind = ATWListViewGetSelectedItem();
        if (ind != -1 && ind < (int)g_AltTabWindows.size()) {
            if (isShiftPressed) {
                AT_LOG_INFO("Shift+Delete Pressed!");
                g_AltTabWindows[ind].IsBeingClosed = true;
                TerminateProcessEx(g_AltTabWindows[ind].PID);
            } else {
                AT_LOG_INFO("Delete Pressed!");
                ATCloseWindow(ind);
            }
        }
        return true;
    }

    // Backtick: move to next/previous window of the same (similar) process.
    if (vkCode == g_Settings.HKBacktickKey) {
        AT_LOG_INFO("Backtick Pressed!, g_IsAltBacktick = %d", g_IsAltBacktick);
        const int direction = isShiftPressed ? -1 : 1;

        const int selectedInd = ATWListViewGetSelectedItem();
        if (selectedInd == -1)
            return true;

        const int N = (int)g_AltTabWindows.size();
        if (N == 0)
            return true;

        const auto& processName = g_AltTabWindows[selectedInd].ProcessName;
        const int pgInd = GetProcessGroupIndex(processName);
        int nextInd = (selectedInd + N + direction) % N;

        // When AltTab was opened with Alt+Backtick, move without group checks.
        if (g_IsAltBacktick) {
            ATWListViewSelectItem(nextInd);
            return true;
        }

        for (int i = 1; i < N; ++i) {
            nextInd = (selectedInd + N + i * direction) % N;
            if (IsSimilarProcess(pgInd, g_AltTabWindows[nextInd].ProcessName)) {
                break;
            }
            if (EqualsIgnoreCase(processName, g_AltTabWindows[nextInd].ProcessName)) {
                break;
            }
            nextInd = -1;
        }

        if (nextInd != -1)
            ATWListViewSelectItem(nextInd);
        return true;
    }

    if (vkCode == VK_F1) {
        DestroyAltTabWindow();
        if (isShiftPressed) {
            DialogBoxW(g_hInstance, MAKEINTRESOURCE(IDD_ABOUTBOX), nullptr, ATAboutDlgProc);
        } else {
            ShowHelpWindow();
        }
        return true;
    }

    if (vkCode == VK_F2) {
        DestroyAltTabWindow();
        if (g_hSettingsWnd == nullptr) {
            DialogBoxW(g_hInstance, MAKEINTRESOURCE(IDD_SETTINGS), nullptr, ATSettingsDlgProc);
        } else {
            SetForegroundWindow(g_hSettingsWnd);
        }
        return true;
    }

    if (vkCode == VK_RETURN) {
        AT_LOG_INFO("Enter Pressed!");
        DestroyAltTabWindow(true);
        return true;
    }

    return false;
}

} // namespace

// ----------------------------------------------------------------------------
// Global definitions (shared with the keyboard hook in AltTab.cpp)
// ----------------------------------------------------------------------------
HWND        g_hListView         = nullptr;
HWND        g_hSearchString     = nullptr;
int         g_nLVHotItem        = -1;
int         g_SelectedIndex     = 0;
std::wstring g_SearchString;
HIMAGELIST  g_hImageList        = nullptr;
HIMAGELIST  g_hLVImageList      = nullptr;
int         g_nImgCloseActiveInd   = -1;
int         g_nImgCloseInactiveInd = -1;

std::vector<AltTabWindowData> g_AltTabWindows;

void AltTabListViewShutdown() {
    if (g_hSSFont) {
        DeleteObject(g_hSSFont);
        g_hSSFont = nullptr;
    }
    if (g_hLVFont) {
        DeleteObject(g_hLVFont);
        g_hLVFont = nullptr;
    }
    if (g_hSSBackgroundBrush) {
        DeleteObject(g_hSSBackgroundBrush);
        g_hSSBackgroundBrush = nullptr;
    }
    g_cachedSSBgColor = 0xFFFFFFFF;
    g_MouseHoverIndex = -1;
    g_SearchString.clear();

    // Any in-flight worker may still publish a stale result; the generation
    // check in AltTabListViewProcessEnumResult drops it, and the next window
    // instance starts its own fresh enumeration.
    g_enumInFlight               = false;
    g_enumPending                = false;
    g_altTabWindowInitialized    = false;
    g_initialSwitchDirection     = 1;
}

// ----------------------------------------------------------------------------
// Search / ListView subclass procedures
// ----------------------------------------------------------------------------

LRESULT CALLBACK SearchStringSubclassProc(
    HWND       hWnd,
    UINT       uMsg,
    WPARAM     wParam,
    LPARAM     lParam,
    UINT_PTR   /*uIdSubclass*/,
    DWORD_PTR  /*dwRefData*/)
{
    switch (uMsg) {
    case WM_KEYDOWN:
    case WM_SYSKEYDOWN: {
        if (HandleCommonAltTabKeys(hWnd, (UINT)wParam)) {
            return TRUE;
        }

        // WM_CHAR won't be sent while ALT is pressed; collect characters here.
        // Ignore Backtick, Tab, Delete, and non-printable characters.
        if ((g_IsAltTab || g_IsAltBacktick) && !g_IsAltCtrlTab) {
            wchar_t ch = L'\0';
            const bool isChar = ATMapVirtualKey((UINT)wParam, ch);
            bool update = false;

            if (isChar && !(wParam == '`' || wParam == VK_DELETE || wParam == VK_TAB || ch == L'\0')) {
                g_SearchString += ch;
                update = true;
            } else if (wParam == VK_BACK && !g_SearchString.empty()) {
                g_SearchString.pop_back();
                update = true;
            }

            if (update) {
                SendMessageW(g_hSearchString, WM_SETTEXT, 0, (LPARAM)g_SearchString.c_str());
                SendMessageW(g_hSearchString, EM_SETSEL, (WPARAM)g_SearchString.size(), (LPARAM)g_SearchString.size());

                // The character is already stored in g_SearchString / the edit
                // control above; consume the matching WM_CHAR to avoid the edit
                // control inserting it a second time.
                return 0;
            }
        }
    } break;

    // Tray context menu for the search string edit control.
    case WM_CONTEXTMENU: {
        AT_LOG_INFO("WM_CONTEXTMENU");
        POINT pt;
        GetCursorPos(&pt);
        const bool result = ShowTrayContextMenu(g_hAltTabWnd, pt);
        if (result) {
            DestroyAltTabWindow();
        }
        return 0;
    }

    // Character input when ALT is NOT pressed (e.g. Alt+Ctrl+Tab mode).
    case WM_CHAR: {
        const wchar_t ch = (wchar_t)wParam;

        // In Alt+Tab mode the WM_KEYDOWN handler already stored the character.
        if ((g_IsAltTab || g_IsAltBacktick) && !g_IsAltCtrlTab) {
            return 0;
        }

        if (!(ch == L'`' || ch == VK_TAB || ch == VK_DELETE || ch == VK_SPACE || ch == L'\0')) {
            LRESULT result = DefSubclassProc(hWnd, uMsg, wParam, lParam);

            wchar_t searchString[_MAX_PATH] = {};
            GetWindowTextW(g_hSearchString, searchString, _MAX_PATH);
            g_SearchString = searchString;
            return result;
        }
        return 0;
    }
    }

    return DefSubclassProc(hWnd, uMsg, wParam, lParam);
}

LRESULT CALLBACK ListViewSubclassProc(
    HWND       hListView,
    UINT       uMsg,
    WPARAM     wParam,
    LPARAM     lParam,
    UINT_PTR   /*uIdSubclass*/,
    DWORD_PTR  /*dwRefData*/)
{
    switch (uMsg) {
    case WM_KEYDOWN:
    case WM_SYSKEYDOWN: {
        if (HandleCommonAltTabKeys(hListView, (UINT)wParam)) {
            return TRUE;
        }
    } break;

    case WM_CHAR: {
        // The main loop runs TranslateMessage over every message, so the
        // synthetic WM_KEYDOWN the low-level hook posts for the backtick key
        // gets turned into a WM_CHAR aimed at the ListView. The ListView never
        // accepts real typed characters (the search edit does), and letting it
        // reach the control's default processing plays the alert beep.
        return 0;
    } break;

    case WM_MOUSEMOVE: {
        TRACKMOUSEEVENT tme;
        tme.cbSize    = sizeof(tme);
        tme.dwFlags   = TME_LEAVE;
        tme.hwndTrack = hListView;
        TrackMouseEvent(&tme);
    } break;

    case WM_MOUSELEAVE: {
        if (g_Settings.ShowProcessInfoTooltip) {
            HideCustomToolTip();
        }
        g_MouseHoverIndex = -1;
        g_nLVHotItem      = -1;
        InvalidateRect(hListView, nullptr, TRUE);
    } break;

    case WM_DRAWITEM: {
        return AT::ATListViewDrawItem(hListView, (LPDRAWITEMSTRUCT)lParam);
    }
    }

    return DefSubclassProc(hListView, uMsg, wParam, lParam);
}

// ----------------------------------------------------------------------------
// ListView data & selection
// ----------------------------------------------------------------------------

static void SizeAltTabWindowToContent() {
    const HWND hHeader = ListView_GetHeader(g_hListView);
    int headerHeight = 0;
    if (hHeader) {
        RECT rcHeader;
        GetClientRect(hHeader, &rcHeader);
        headerHeight = rcHeader.bottom - rcHeader.top;
    }

    RECT rcListView = {};
    GetClientRect(g_hListView, &rcListView);
    const int itemHeight =
        ListView_GetItemRect(g_hListView, 0, &rcListView, LVIR_BOUNDS) ? rcListView.bottom - rcListView.top : 0;
    const int itemCount = ListView_GetItemCount(g_hListView);

    RECT rcSearch = {};
    const int searchStringHeight = g_Settings.ShowSearchString && g_hSearchString
        ? (GetClientRect(g_hSearchString, &rcSearch) ? rcSearch.bottom - rcSearch.top : 0)
        : -1;

    const int windowWidth  = g_Settings.WindowWidth;
    const int windowHeight = g_Settings.WindowHeight;
    // The preview is a separate window beside the switcher; keep the switcher
    // itself at the list width but center the combined block on screen.
    const int totalWidth   = windowWidth + GetWindowPreviewWidth(windowWidth);
    const int screenWidth  = GetSystemMetrics(SM_CXSCREEN);
    const int screenHeight = GetSystemMetrics(SM_CYSCREEN);
    const int windowX = (screenWidth - totalWidth) / 2;
    const int windowY = (screenHeight - windowHeight) / 2;

    int requiredHeight = itemHeight * itemCount + headerHeight + searchStringHeight + 2;

    if (requiredHeight <= windowHeight) {
        SetWindowPos(g_hAltTabWnd, HWND_TOPMOST, windowX, windowY, windowWidth, requiredHeight, SWP_NOZORDER);
        WindowResizeAndPosition(g_hAltTabWnd, totalWidth, requiredHeight);
    } else {
        const int scrollBarWidth = GetSystemMetrics(SM_CXVSCROLL);
        const int processNameWidth = GetColProcessNameWidth();
        const int colTitleWidth = windowWidth - (COL_ICON_WIDTH + processNameWidth) - scrollBarWidth - 2;

        if (itemHeight > 0) {
            const int numberOfVisibleItems = (windowHeight - itemHeight + 1) / itemHeight;
            const int boundedLvHeight = numberOfVisibleItems * itemHeight + headerHeight;
            requiredHeight = boundedLvHeight + searchStringHeight + 2;

            ListView_SetColumnWidth(g_hListView, 1, colTitleWidth);

            SetWindowPos(
                g_hListView,
                nullptr,
                0,
                0,
                windowWidth - 2,
                boundedLvHeight,
                SWP_NOMOVE | SWP_NOZORDER);
        }

        SetWindowPos(g_hAltTabWnd, HWND_TOPMOST, windowX, windowY, windowWidth, requiredHeight, SWP_NOZORDER);
        WindowResizeAndPosition(g_hAltTabWnd, totalWidth, requiredHeight);
    }

    // Column widths mix DPI-scaled and raw constants, so their sum can exceed
    // the (DPI-scaled) client width and pop a horizontal scrollbar. Pin the
    // title column to what actually fits the final client area.
    RECT rcFit = {};
    GetClientRect(g_hListView, &rcFit);
    const int fitWidth = (rcFit.right - rcFit.left) - ListView_GetColumnWidth(g_hListView, 0)
                         - (g_Settings.ShowColProcessName ? ListView_GetColumnWidth(g_hListView, 2) : 0)
                         - GetSystemMetrics(SM_CXVSCROLL) - 2;
    if (fitWidth > 60) {
        ListView_SetColumnWidth(g_hListView, 1, fitWidth);
    }
}

// Build the ListView rows + image list from the given enumeration result.
static void ApplyEnumResults(std::vector<AltTabWindowData> windows) {
    const bool isFirstPopulation = !g_altTabWindowInitialized;

    // The freshness timer keeps enumerating even while the desktop is idle;
    // skip the rebuild when nothing changed.
    if (!isFirstPopulation && windows.size() == g_AltTabWindows.size()
        && std::equal(windows.begin(), windows.end(), g_AltTabWindows.begin())) {
        return;
    }

    ListView_DeleteAllItems(g_hListView);
    g_AltTabWindows = std::move(windows);

    // Mark processes that are running from different paths with the same
    // process name + title (useful for "_xplorer2.exe" style duplicates).
    std::unordered_map<std::wstring, std::unordered_set<std::wstring>> processMap;
    for (const auto& item : g_AltTabWindows) {
        processMap[item.ProcessName + item.Title].insert(item.FullPath);
    }
    for (auto& item : g_AltTabWindows) {
        if (processMap[item.ProcessName + item.Title].size() > 1) {
            item.IsConflictProcess = true;
        }
    }

    const int imageWidth  = GetSystemMetrics(SM_CXICON);
    const int imageHeight = GetSystemMetrics(SM_CYICON);

    // A "dummy" image list sets the row height; the real icons are drawn
    // manually because we need per-row control over the icon.
    HIMAGELIST hImageListDummy = ImageList_Create(imageWidth, imageHeight + 1, ILC_COLOR32 | ILC_MASK, 0, 1);
    HIMAGELIST hImageList = ImageList_Create(imageWidth, imageHeight, ILC_COLOR32 | ILC_MASK, 0, 1);

    if (g_hLVImageList) {
        ImageList_Destroy(g_hLVImageList);
        g_hLVImageList = nullptr;
    }
    HIMAGELIST hOldDummy = ListView_GetImageList(g_hListView, LVSIL_SMALL);
    if (hOldDummy) {
        ImageList_Destroy(hOldDummy);
    }

    for (const auto& item : g_AltTabWindows) {
        ImageList_AddIcon(hImageList, item.hIcon);
    }

    ListView_SetImageList(g_hListView, hImageListDummy, LVSIL_SMALL);
    g_hLVImageList = hImageList;

    for (int i = 0; i < (int)g_AltTabWindows.size(); ++i) {
        AddListViewItem(g_hListView, i, g_AltTabWindows[i]);
    }

    if (isFirstPopulation) {
        g_altTabWindowInitialized = true;

        SizeAltTabWindowToContent();

        // A quick tap releases Alt before the asynchronous enumeration has
        // returned. Now that the list finally exists, complete the switch
        // immediately instead of showing the switcher and waiting for the
        // Alt-key-up timer to close it again.
        if (!(GetAsyncKeyState(VK_MENU) & 0x8000)) {
            if (!g_AltTabWindows.empty()) {
                const int N = (int)g_AltTabWindows.size();
                ATWListViewSelectItem((N + g_initialSwitchDirection) % N);
            } else {
                g_SelectedIndex = -1;
            }
            DestroyAltTabWindow(true);
            return;
        }

        // Size/position and show the switcher + preview panel first, then pick
        // the initial row (which registers the DWM thumbnail on a visible
        // destination).
        LayoutWindowPreview(g_hAltTabWnd, g_Settings.WindowWidth);
        ShowWindow(g_hAltTabWnd, SW_SHOWNORMAL);
        ShowWindowPreview(true);

        if (!g_AltTabWindows.empty()) {
            // The window was opened by the Alt+Tab key press whose direction we
            // captured; the old synchronous flow highlighted row 0 first and
            // then moved to (0 + N + direction) % N on the opening press, so
            // land directly on that row now that the list has arrived.
            const int N = (int)g_AltTabWindows.size();
            ATWListViewSelectItem((N + g_initialSwitchDirection) % N);
        } else {
            g_SelectedIndex = -1;
        }

        // Give it focus (it was created hidden so it never flashes at the
        // provisional size).
        SetForegroundWindow(g_hAltTabWnd);
        SetFocus(g_hSearchString);
    } else {
        ATWListViewSelectItem(g_SelectedIndex);
    }
}

void AltTabListViewSetInitialDirection(int direction) {
    g_initialSwitchDirection = direction;
}

int AltTabListViewGetInitialDirection() {
    return g_initialSwitchDirection;
}

bool AltTabListViewInitialPopulationApplied() {
    return g_altTabWindowInitialized;
}

void AltTabListViewProcessEnumResult() {
    AT_LOG_TRACE;

    std::shared_ptr<EnumResult> result;
    {
        std::lock_guard<std::mutex> lock(g_enumSlotMutex);
        result = std::move(g_enumSlot);
        g_enumSlot.reset();
    }

    // No result (a stale notification after destroy/recreate): just settle the
    // bookkeeping and roll any pending refresh.
    if (!result) {
        g_enumInFlight = false;
        if (g_enumPending) {
            g_enumPending = false;
            RefreshAltTabWindow();
        }
        return;
    }

    // A worker that outlived the requesting window/refresh cycle must not
    // overwrite fresher data.
    if (result->generation != g_enumGeneration) {
        g_enumInFlight = false;
        if (g_enumPending) {
            g_enumPending = false;
            RefreshAltTabWindow();
        }
        return;
    }

    ApplyEnumResults(std::move(result->windows));

    g_enumInFlight = false;
    if (g_enumPending) {
        g_enumPending = false;
        RefreshAltTabWindow();
    }
}

void AltTabListViewStopAsyncEnumeration() {
    g_enumShutdown.store(true);
    if (g_enumWorker.joinable()) {
        // The worker's cross-process probes are all time-limited; joining here
        // is bounded and prevents it from colliding with CRT teardown.
        g_enumWorker.join();
    }
}

void RefreshAltTabWindow() {
    AT_LOG_TRACE;

    if (g_enumShutdown.load() || g_hAltTabWnd == nullptr) {
        return;
    }

    // Coalesce: the freshness timer (100 ms in Release) can fire while a
    // worker is still enumerating; remember that a refresh was requested and
    // start exactly one more pass once the current one finishes.
    if (g_enumInFlight) {
        g_enumPending = true;
        return;
    }
    g_enumInFlight = true;

    const DWORD generation = ++g_enumGeneration;
    const AltTabEnumContext context = CaptureAltTabEnumContext();
    const HWND hTargetWindow = g_hAltTabWnd;

    // Single-flight (see g_enumInFlight above), so any previous worker kept in
    // g_enumWorker has already finished; join it before reusing the slot.
    if (g_enumWorker.joinable()) {
        g_enumWorker.join();
    }

    g_enumWorker = std::thread([generation, context, hTargetWindow]() {
        auto result = std::make_shared<EnumResult>();
        result->generation = generation;
        result->windows    = EnumerateAltTabWindows(context);
        {
            std::lock_guard<std::mutex> lock(g_enumSlotMutex);
            g_enumSlot = result;
        }
        PostMessageW(hTargetWindow, WM_USER_ALTTAB_ENUM_DONE, 0, 0);
    });
}

void ATWListViewSelectItem(int rowNumber) {
    if (g_AltTabWindows.empty()) {
        g_SelectedIndex = -1;
        return;
    }

    const int maxRow = (int)g_AltTabWindows.size() - 1;

    // Clear the current selection first.
    const int selectedInd = (int)SendMessageW(g_hListView, LVM_GETNEXTITEM, (WPARAM)-1, LVNI_SELECTED);
    LVITEM lvItem = {};
    lvItem.stateMask = LVIS_FOCUSED | LVIS_SELECTED;
    lvItem.state     = 0;
    SendMessageW(g_hListView, LVM_SETITEMSTATE, selectedInd, (LPARAM)&lvItem);

    rowNumber = max(0, min(rowNumber, maxRow));

    lvItem.state = LVIS_FOCUSED | LVIS_SELECTED;
    SendMessageW(g_hListView, LVM_SETITEMSTATE, rowNumber, (LPARAM)&lvItem);
    SendMessageW(g_hListView, LVM_ENSUREVISIBLE, rowNumber, (LPARAM)FALSE);

    g_SelectedIndex = rowNumber;

    // Preview of the newly selected window is captured on a worker thread.
    RequestWindowPreview();
}

void ATWListViewSelectPrevItem() {
    const int selectedInd = (int)SendMessageW(g_hListView, LVM_GETNEXTITEM, (WPARAM)-1, LVNI_SELECTED);
    if (selectedInd == -1)
        return;
    const int N = (int)g_AltTabWindows.size();
    if (N == 0)
        return;

    const int prevInd = (selectedInd + N - 1) % N;

    LVITEM lvItem = {};
    lvItem.stateMask = LVIS_FOCUSED | LVIS_SELECTED;
    lvItem.state     = 0;
    SendMessageW(g_hListView, LVM_SETITEMSTATE, selectedInd, (LPARAM)&lvItem);

    lvItem.state = LVIS_FOCUSED | LVIS_SELECTED;
    SendMessageW(g_hListView, LVM_SETITEMSTATE, prevInd, (LPARAM)&lvItem);
    SendMessageW(g_hListView, LVM_ENSUREVISIBLE, prevInd, (LPARAM)FALSE);

    g_SelectedIndex = prevInd;

    RequestWindowPreview();
}

void ATWListViewSelectNextItem() {
    const int selectedInd = (int)SendMessageW(g_hListView, LVM_GETNEXTITEM, (WPARAM)-1, LVNI_SELECTED);
    if (selectedInd == -1)
        return;
    const int N = (int)g_AltTabWindows.size();
    if (N == 0)
        return;

    const int nextInd = (selectedInd + N + 1) % N;

    LVITEM lvItem = {};
    lvItem.stateMask = LVIS_FOCUSED | LVIS_SELECTED;
    lvItem.state     = 0;
    SendMessageW(g_hListView, LVM_SETITEMSTATE, selectedInd, (LPARAM)&lvItem);

    lvItem.state = LVIS_FOCUSED | LVIS_SELECTED;
    SendMessageW(g_hListView, LVM_SETITEMSTATE, nextInd, (LPARAM)&lvItem);
    SendMessageW(g_hListView, LVM_ENSUREVISIBLE, nextInd, (LPARAM)FALSE);

    g_SelectedIndex = nextInd;

    RequestWindowPreview();
}

void ATWListViewDeleteItem(int rowNumber) {
    SendMessageW(g_hListView, LVM_DELETEITEM, rowNumber, 0);
    ATWListViewSelectItem(rowNumber);
}

int ATWListViewGetSelectedItem() {
    return (int)SendMessageW(g_hListView, LVM_GETNEXTITEM, (WPARAM)-1, LVNI_SELECTED);
}

bool ATMapVirtualKey(UINT uCode, wchar_t& vkCode) {
    wchar_t ch = static_cast<wchar_t>(uCode);

    if (!iswprint((wint_t)uCode)) {
        vkCode = L'\0';
        return false;
    }

    const bool isShiftPressed = (GetKeyState(VK_SHIFT) & 0x8000) != 0;

    // Alphabetic and digit keys.
    if ((uCode >= 'A' && uCode <= 'Z') || (uCode >= '0' && uCode <= '9')) {
        ch = (wchar_t)MapVirtualKey(uCode, MAPVK_VK_TO_CHAR);

        if (!isShiftPressed && ch >= L'A' && ch <= L'Z') {
            ch = ch - L'A' + L'a';
        }

        if (isShiftPressed && ch >= L'0' && ch <= L'9') {
            ch = L")!@#$%^&*("[ch - L'0'];
        }

        vkCode = ch;
        return true;
    }

    // Special characters. OEM mappings assume the US keyboard layout.
    switch (uCode) {
        case VK_SPACE:      vkCode = L' ';                          return true;
        case VK_OEM_MINUS:  vkCode = isShiftPressed ? L'_' : L'-';  return true;
        case VK_OEM_PLUS:   vkCode = isShiftPressed ? L'+' : L'=';  return true;
        case VK_OEM_1:      vkCode = isShiftPressed ? L':' : L';';  return true;
        case VK_OEM_2:      vkCode = isShiftPressed ? L'?' : L'/';  return true;
        case VK_OEM_3:      vkCode = isShiftPressed ? L'~' : L'`';  return true;
        case VK_OEM_4:      vkCode = isShiftPressed ? L'{' : L'[';  return true;
        case VK_OEM_5:      vkCode = isShiftPressed ? L'|' : L'\\'; return true;
        case VK_OEM_6:      vkCode = isShiftPressed ? L'}' : L']';  return true;
        case VK_OEM_7:      vkCode = isShiftPressed ? L'"' : L'\''; return true;
        case VK_OEM_COMMA:  vkCode = isShiftPressed ? L'<' : L',';  return true;
        case VK_OEM_PERIOD: vkCode = isShiftPressed ? L'>' : L'.';  return true;
        case VK_OEM_102:    vkCode = isShiftPressed ? L'>' : L'<';  return true;
    }
    return false;
}

// ----------------------------------------------------------------------------
// Window / process termination
// ----------------------------------------------------------------------------

bool TerminateProcessEx(DWORD pid) {
    HANDLE hProcess = OpenProcess(PROCESS_TERMINATE, FALSE, pid);
    if (hProcess) {
        TerminateProcess(hProcess, 0);
        CloseHandle(hProcess);
        return true;
    }
    return false;
}

void ATCloseWindow(const int index) {
    if (index < 0 || index >= (int)g_AltTabWindows.size()) {
        return;
    }

    AltTabWindowData& windowData = g_AltTabWindows[index];

#ifdef _DEBUG
    // Ignore the developer environment processes in debug builds.
    static const wchar_t* const IGNORED_IN_DEBUG[] = {
        L"devenv.exe", L"Code.exe", L"Outlook.exe", L"xplorer2_64.exe", L"msedge.exe", L"OneNote.exe", L"Fork.exe",
        L"ConEmu64.exe", L"ms-teams.exe", L"AltTab.exe"
    };
    for (const wchar_t* ignored : IGNORED_IN_DEBUG) {
        if (EqualsIgnoreCase(windowData.ProcessName, ignored)) {
            return;
        }
    }
#endif // _DEBUG

    windowData.IsBeingClosed = true;
    AT_LOG_INFO(
        "ATCloseWindow: index: %d, hWnd: %#x, hOwner: %#x, Title: %ls",
        index,
        windowData.hWnd,
        windowData.hOwner,
        windowData.Title.c_str());

    ListView_RedrawItems(g_hListView, index, index);
    UpdateWindow(g_hListView);
    Sleep(50);

    // Sending SC_CLOSE to a slow/hung window must not block the AltTab UI
    // thread; every request is time-limited (and hung windows are skipped).
    if (IsWindow(windowData.hWnd) && !IsHungAppWindowEx(windowData.hWnd)) {
        DWORD_PTR result = 0;
        SendMessageTimeoutW(
            windowData.hWnd,
            WM_SYSCOMMAND,
            SC_CLOSE,
            0,
            SMTO_ABORTIFHUNG,
            kCloseReplyTimeoutMs,
            &result);
    }
    Sleep(50);

    if (windowData.hOwner != windowData.hWnd && IsWindow(windowData.hOwner) && !IsHungAppWindowEx(windowData.hOwner)) {
        DWORD_PTR result = 0;
        SendMessageTimeoutW(
            windowData.hOwner,
            WM_SYSCOMMAND,
            SC_CLOSE,
            0,
            SMTO_ABORTIFHUNG,
            kCloseReplyTimeoutMs,
            &result);
    }
}

// ----------------------------------------------------------------------------
// Search string colors
// ----------------------------------------------------------------------------

LRESULT AltTabSearchControlColor(HDC hDC, bool /*isEditControl*/) {
    const COLORREF textColor = g_Settings.SSFontColor;
    const COLORREF bgColor   = g_Settings.SSBackgroundColor;

    SetTextColor(hDC, textColor);
    SetBkColor(hDC, bgColor);

    if (bgColor != g_cachedSSBgColor || !g_hSSBackgroundBrush) {
        if (g_hSSBackgroundBrush) {
            DeleteObject(g_hSSBackgroundBrush);
        }
        g_hSSBackgroundBrush = CreateSolidBrush(bgColor);
        g_cachedSSBgColor = bgColor;
    }

    return (LRESULT)g_hSSBackgroundBrush;
}

// ----------------------------------------------------------------------------
// Owner-drawn ListView
// ----------------------------------------------------------------------------

namespace AT {

void DrawTextWithHighlight(
    HDC hdc,
    const RECT& rcSubItem,
    const COLORREF& textColor,
    const std::wstring& itemText,
    const std::set<std::pair<size_t, size_t>>& highlights)
{
    static const UINT format = DT_LEFT | DT_SINGLELINE | DT_VCENTER | DT_END_ELLIPSIS;

    RECT rci = rcSubItem;
    rci.top += 3;
    rci.bottom -= 3;

    int xPos = rci.left;
    size_t prev = 0;

    for (const auto& pr : highlights) {
        if (pr.second < prev || pr.second >= itemText.size())
            continue;

        const size_t startInd = prev > pr.first ? prev : pr.first;
        if (startInd >= itemText.size())
            break;
        const size_t wordLen = pr.second - startInd + 1;

        // Draw text before the match.
        const int beforeMatchLen = (int)(startInd - prev);
        SIZE beforeSize = {};
        GetTextExtentPointW(hdc, itemText.c_str() + prev, beforeMatchLen, &beforeSize);

        SetTextColor(hdc, textColor);
        DrawTextW(hdc, itemText.c_str() + prev, beforeMatchLen, &rci, format);

        xPos += beforeSize.cx;
        rci.left = xPos;

        if (rci.left >= rcSubItem.right)
            break;

        // Highlight the matching part.
        const int matchTextLen = (int)wordLen;
        SIZE matchSize = {};
        GetTextExtentPointW(hdc, itemText.c_str() + startInd, matchTextLen, &matchSize);

        RECT matchRect = rci;
        matchRect.right = AT_MIN(matchRect.right, matchRect.left + matchSize.cx);
        HBRUSH hbr = CreateSolidBrush(g_Settings.LVHighlightBackgroundColor);
        FillRect(hdc, &matchRect, hbr);
        DeleteObject(hbr);

        SetTextColor(hdc, g_Settings.LVHighlightTextColor);
        DrawTextW(hdc, itemText.c_str() + startInd, matchTextLen, &matchRect, format);

        xPos += matchSize.cx;
        rci.left = xPos;

        if (rci.left >= rcSubItem.right)
            break;

        prev = pr.second + 1;
    }

    // Draw the remaining text.
    const int remainingTextLen = (int)(itemText.size() - prev);
    if (remainingTextLen > 0) {
        SetTextColor(hdc, textColor);
        DrawTextW(hdc, itemText.c_str() + prev, remainingTextLen, &rci, format);
    }
}

BOOL ATListViewDrawItem(HWND hListView, LPDRAWITEMSTRUCT lpDrawItemStruct) {
    HDC& hdc = lpDrawItemStruct->hDC;
    const RECT rcItem = lpDrawItemStruct->rcItem;
    const int rowIndex = lpDrawItemStruct->itemID;

    if (rowIndex < 0)
        return FALSE;

    LVITEM lvItem = {};
    lvItem.iItem = rowIndex;
    lvItem.mask  = LVIF_TEXT | LVIF_IMAGE | LVIF_PARAM;
    ListView_GetItem(hListView, &lvItem);

    const AltTabWindowData* pWindowData = (AltTabWindowData*)lvItem.lParam;
    if (pWindowData == nullptr)
        return FALSE;

    const HWND hHeader = ListView_GetHeader(hListView);
    const int columns = hHeader ? Header_GetItemCount(hHeader) : 0;

    COLORREF clrBk   = ListView_GetBkColor(hListView);
    COLORREF clrText = ListView_GetTextColor(hListView);

    if (lpDrawItemStruct->itemState & ODS_SELECTED) {
        clrBk   = GetSysColor(COLOR_HIGHLIGHT);
        clrText = GetSysColor(COLOR_HIGHLIGHTTEXT);
    }

    if (pWindowData->IsBeingClosed) {
        clrBk   = RGB(255, 69, 54); // Red Orange
        clrText = RGB(0, 0, 0);
    }

    // Gradient background.
    TRIVERTEX vertex[2];
    vertex[0].x = rcItem.left + 1;
    vertex[0].y = rcItem.top + 1;
    vertex[1].x = rcItem.right - 1;
    vertex[1].y = rcItem.bottom - 1;
    vertex[0].Red   = vertex[1].Red   = GetRValue(clrBk) << 8;
    vertex[0].Green = vertex[1].Green = GetGValue(clrBk) << 8;
    vertex[0].Blue  = vertex[1].Blue  = GetBValue(clrBk) << 8;
    vertex[0].Alpha = vertex[1].Alpha = 0;

    GRADIENT_RECT gRect = { 0, 1 };
    GradientFill(hdc, vertex, 2, &gRect, 1, GRADIENT_FILL_RECT_V);

    // Vertical separator between the title and process-name columns.
    if (columns == 3) {
        RECT rcSub;
        ListView_GetSubItemRect(hListView, rowIndex, 2, LVIR_BOUNDS, &rcSub);
        SetBkMode(hdc, TRANSPARENT);

        HPEN hPen = CreatePen(PS_SOLID, 1, RGB(90, 90, 90));
        HPEN hOldPen = (HPEN)SelectObject(hdc, hPen);
        MoveToEx(hdc, rcSub.left - 1, rcSub.top, nullptr);
        LineTo(hdc, rcSub.left - 1, rcSub.bottom + 1);
        SelectObject(hdc, hOldPen);
        DeleteObject(hPen);
    }

    if (lpDrawItemStruct->itemState & ODS_SELECTED) {
        HBRUSH hbr = CreateSolidBrush(RGB(201, 201, 201));
        FrameRect(hdc, &rcItem, hbr);
        DeleteObject(hbr);
    }

    // Hot-track outline.
    if (g_Settings.ShowHighlightRect && (lpDrawItemStruct->itemState & ODS_HOTLIGHT || g_nLVHotItem == rowIndex)) {
        HBRUSH hbr = CreateSolidBrush(RGB(100, 149, 237)); // Cornflower Blue
        FrameRect(hdc, &rcItem, hbr);
        DeleteObject(hbr);
    }

    SetTextColor(hdc, clrText);

    for (int col = 0; col < columns; ++col) {
        RECT rcSub;
        ListView_GetSubItemRect(hListView, rowIndex, col, LVIR_BOUNDS, &rcSub);
        SetBkMode(hdc, TRANSPARENT);

        if (col == 0) {
            if (lvItem.iImage >= 0 && g_hLVImageList && lvItem.iImage < ImageList_GetImageCount(g_hLVImageList)) {
                const int rowHeight = rcSub.bottom - rcSub.top;
                const int iconSize = 32;
                const int x = rcSub.left + 2;
                const int y = rcSub.top + (rowHeight - iconSize) / 2;

                ImageList_DrawEx(
                    g_hLVImageList, lvItem.iImage, hdc, x, y, iconSize, iconSize, CLR_NONE, CLR_NONE, ILD_NORMAL);
            }
        } else if (col == 1) {
            rcSub.left += 3;

            if (pWindowData->IsConflictProcess) {
                const std::wstring title = pWindowData->Title + L" - [v" + pWindowData->Version + L"]";
                DrawTextWithHighlight(hdc, rcSub, clrText, title, pWindowData->TitleHighlights);
            } else {
                DrawTextWithHighlight(hdc, rcSub, clrText, pWindowData->Title, pWindowData->TitleHighlights);
            }
        } else if (col == 2) {
            rcSub.left += 4;
            DrawTextWithHighlight(
                hdc, rcSub, clrText, pWindowData->ProcessName, pWindowData->ProcessNameHighlights);
        }
    }

    // Close button on hot-tracked rows.
    if (g_Settings.ShowDeleteButton && (lpDrawItemStruct->itemState & ODS_HOTLIGHT || g_nLVHotItem == rowIndex)) {
        g_rcBtnClose.left   = rcItem.right - g_nIconSize - 1;
        g_rcBtnClose.top    = rcItem.top + 1;
        g_rcBtnClose.right  = g_rcBtnClose.left + g_nIconSize;
        g_rcBtnClose.bottom = g_rcBtnClose.top + g_nIconSize;

        const int imgIndex = g_IsMouseOverCloseButton ? g_nImgCloseActiveInd : g_nImgCloseInactiveInd;
        ImageList_DrawEx(
            g_hImageList,
            imgIndex,
            hdc,
            g_rcBtnClose.left,
            g_rcBtnClose.top,
            g_nIconSize,
            g_nIconSize,
            CLR_NONE,
            CLR_NONE,
            ILD_TRANSPARENT);
    }

    return TRUE;
}

} // namespace AT

// ----------------------------------------------------------------------------
// WM_NOTIFY / hot-track / tooltip / click handling
// ----------------------------------------------------------------------------

void AltTabWindow_OnNotify(HWND /*hwnd*/, NMHDR* pnmhdr) {
    if (pnmhdr->hwndFrom == g_hListView && pnmhdr->code == LVN_HOTTRACK) {
        LPNMLISTVIEW pnmListView = reinterpret_cast<LPNMLISTVIEW>(pnmhdr);

        // With LVS_OWNERDRAWFIXED iItem may be -1; use a hit-test instead.
        POINT pt;
        GetCursorPos(&pt);
        const POINT ptScreenPos = pt;

        ScreenToClient(g_hListView, &pt);
        const POINT ptClientPos = pt;

        if (pnmListView->iItem < 0) {
            LVHITTESTINFO ht = {};
            ht.pt = ptClientPos;
            if (ListView_SubItemHitTest(g_hListView, &ht) != -1) {
                pnmListView->iItem = ht.iItem;
            }
        }

        // Close-button hover state.
        if (g_Settings.ShowDeleteButton) {
            const bool overClose =
                g_rcBtnClose.left <= ptClientPos.x && ptClientPos.x <= g_rcBtnClose.right
                && g_rcBtnClose.top <= ptClientPos.y && ptClientPos.y <= g_rcBtnClose.bottom;
            if (overClose != g_IsMouseOverCloseButton) {
                g_IsMouseOverCloseButton = overClose;
                InvalidateRect(g_hListView, &g_rcBtnClose, FALSE);
            }
        }

        // Redraw only the old + new hot rows.
        if (pnmListView->iItem != g_nLVHotItem) {
            const int oldItem = g_nLVHotItem;
            g_nLVHotItem = pnmListView->iItem;

            if (oldItem >= 0) {
                RECT rc;
                ListView_GetItemRect(g_hListView, oldItem, &rc, LVIR_BOUNDS);
                InvalidateRect(g_hListView, &rc, FALSE);
            }
            if (g_nLVHotItem >= 0) {
                RECT rc;
                ListView_GetItemRect(g_hListView, g_nLVHotItem, &rc, LVIR_BOUNDS);
                InvalidateRect(g_hListView, &rc, FALSE);
            }
        }

        // Process-info tooltip.
        if (g_Settings.ShowProcessInfoTooltip && pnmListView->iItem >= 0
            && (pnmListView->iItem != g_MouseHoverIndex || !IsTooltipVisible())) {
            g_MouseHoverIndex = pnmListView->iItem;

            HideCustomToolTip();

            std::wstring tooltip = std::format(
                L"Title: {}\nPath: {}\nDescription: {} {}\nCompanyName: {}\nPID: {}",
                g_AltTabWindows[g_MouseHoverIndex].Title,
                g_AltTabWindows[g_MouseHoverIndex].FullPath,
                g_AltTabWindows[g_MouseHoverIndex].Description,
                g_AltTabWindows[g_MouseHoverIndex].Version,
                g_AltTabWindows[g_MouseHoverIndex].CompanyName,
                g_AltTabWindows[g_MouseHoverIndex].PID);

#ifdef _DEBUG
            tooltip += std::format(L"\nHWND: 0x{:08X}", (UINT_PTR)g_AltTabWindows[g_MouseHoverIndex].hWnd);
#endif // _DEBUG

            RECT itemRect;
            if (ListView_GetItemRect(g_hListView, g_MouseHoverIndex, &itemRect, LVIR_BOUNDS)) {
                pt.x = ptClientPos.x;
                pt.y = itemRect.bottom + 1;
                ClientToScreen(g_hListView, &pt);
                pt.x = ptScreenPos.x + 36;
                ShowCustomToolTipAt(tooltip, pt, -1);
            }
        }
        return;
    }

    // Single click.
    if (pnmhdr->hwndFrom == g_hListView && pnmhdr->code == NM_CLICK) {
        if (g_IsMouseOverCloseButton) {
            if (g_nLVHotItem != -1) {
                ATCloseWindow(g_nLVHotItem);
            }
        } else {
            DestroyAltTabWindow(true);
        }
    }
}

// ----------------------------------------------------------------------------
// AltTab window layout & control creation
// ----------------------------------------------------------------------------

void InitListView(HWND hWnd) {
    AT_LOG_TRACE;

    const int screenWidth  = GetSystemMetrics(SM_CXSCREEN);
    const int screenHeight = GetSystemMetrics(SM_CYSCREEN);

    const int windowWidth  = (int)(screenWidth * g_Settings.WidthPercentage * 0.01);
    const int windowHeight = (int)(screenHeight * g_Settings.HeightPercentage * 0.01);

    const int windowY = (screenHeight - windowHeight) / 2;

    DWORD style = WS_VISIBLE | WS_CHILD | LVS_REPORT | LVS_SHOWSELALWAYS | LVS_OWNERDRAWFIXED;
    if (!g_Settings.ShowColHeader) {
        style |= LVS_NOCOLUMNHEADER;
    }

    // Resolve fonts.
    HDC hdc = GetDC(hWnd);
    if (g_hSSFont == nullptr) {
        g_hSSFont = CreateFontEx(hdc, g_Settings.SSFontName, g_Settings.SSFontSize, g_Settings.SSFontStyle);
    }
    if (g_hLVFont == nullptr) {
        g_hLVFont = CreateFontEx(hdc, g_Settings.LVFontName, g_Settings.LVFontSize, g_Settings.LVFontStyle);
    }

    SelectObject(hdc, g_hSSFont);
    TEXTMETRIC tm = {};
    GetTextMetrics(hdc, &tm);
    int searchStringHeight = tm.tmHeight + tm.tmExternalLeading;

    const int dpi = GetDeviceCaps(hdc, LOGPIXELSX);
    ReleaseDC(hWnd, hdc);

    if (!g_Settings.ShowSearchString) {
        searchStringHeight = 0;
    }

    // Search string edit control.
    const DWORD searchStringTextStyles = WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_CENTER;
    HWND hSearchString = CreateWindowW(
        WC_EDIT,
        L"",
        searchStringTextStyles,
        0,
        0,
        windowWidth,
        searchStringHeight,
        hWnd,
        (HMENU)IDC_ALTTAB_EDIT,
        g_hInstance,
        nullptr);
    SendMessageW(hSearchString, WM_SETFONT, (WPARAM)g_hSSFont, TRUE);
    g_hSearchString = hSearchString;

    SendMessageW(hSearchString, EM_SETCUEBANNER, (WPARAM)TRUE, (LPARAM)g_Settings.SSCueBannerText.c_str());
    SetWindowSubclass(hSearchString, SearchStringSubclassProc, 1, 0);

    // Layout: 1px gap between the search control and the ListView.
    int yPos = 0;
    if (g_Settings.ShowSearchString) {
        yPos += searchStringHeight;
    } else {
        searchStringHeight = -1;
    }

    // Window width reduced by 1 to fit the scrollbar; height reduced by 3
    // (1px overlap + 2px borders).
    const int lvWidth  = windowWidth - 1;
    const int lvHeight = windowHeight - searchStringHeight - 3;

    HWND hListView = CreateWindowExW(
        0,
        WC_LISTVIEW,
        L"",
        style,
        0,
        yPos,
        lvWidth,
        lvHeight,
        hWnd,
        (HMENU)IDC_LISTVIEW,
        g_hInstance,
        nullptr);
    g_hListView = hListView;

    SendMessageW(hListView, WM_SETFONT, (WPARAM)g_hLVFont, MAKELPARAM(TRUE, 0));
    SetWindowSubclass(hListView, ListViewSubclassProc, 1, 0);

    // Preview panel to the right of the ListView (a live DWM thumbnail).
    if (IsWindowPreviewEnabled()) {
        CreateWindowPreview();
    }

    g_Settings.WindowWidth  = windowWidth;
    g_Settings.WindowHeight = windowHeight;

    CustomizeListView(hListView, dpi);
    SetListViewCustomColors(hListView, g_Settings.LVBackgroundColor, g_Settings.LVFontColor);

    // Window transparency.
    SetWindowLong(hWnd, GWL_EXSTYLE, GetWindowLong(hWnd, GWL_EXSTYLE) | WS_EX_LAYERED);
    SetLayeredWindowAttributes(hWnd, RGB(255, 255, 255), (BYTE)g_Settings.Transparency, LWA_ALPHA);

    // Kick off the asynchronous enumeration; the first result populates the
    // ListView, sizes the window to fit, and shows it (it is created hidden to
    // avoid flashing the provisional size).
    RefreshAltTabWindow();

    // Provisional size until the first enumeration result arrives. The switcher
    // keeps the list width; the preview (a separate window) is centered with it.
    const int totalWidth = windowWidth + GetWindowPreviewWidth(windowWidth);
    const int totalX = (screenWidth - totalWidth) / 2;
    SetWindowPos(hWnd, HWND_TOPMOST, totalX, windowY, windowWidth, windowHeight, SWP_NOZORDER);
    LayoutWindowPreview(hWnd, windowWidth);

    // Keep the list fresh while the window stays open.
    SetTimer(hWnd, TIMER_WINDOW_COUNT, TIMER_WINDOW_COUNT_ELAPSE, nullptr);
}

// ----------------------------------------------------------------------------
// Image list (close button)
// ----------------------------------------------------------------------------

HBITMAP LoadPngAsHBITMAP(HINSTANCE hInst, int resID, int cx, int cy) {
    HRSRC hRes = FindResource(hInst, MAKEINTRESOURCE(resID), RT_RCDATA);
    if (!hRes)
        return nullptr;

    const DWORD imageSize = SizeofResource(hInst, hRes);
    HGLOBAL hMem = LoadResource(hInst, hRes);
    if (!hMem)
        return nullptr;

    const void* pImageData = LockResource(hMem);

    HGLOBAL hBuffer = GlobalAlloc(GMEM_MOVEABLE, imageSize);
    if (!hBuffer)
        return nullptr;

    void* pBuffer = GlobalLock(hBuffer);
    if (!pBuffer) {
        GlobalFree(hBuffer);
        return nullptr;
    }
    memcpy(pBuffer, pImageData, imageSize);
    GlobalUnlock(hBuffer);

    IStream* pStream = nullptr;
    CreateStreamOnHGlobal(hBuffer, TRUE, &pStream);

    Gdiplus::Bitmap* bmp = Gdiplus::Bitmap::FromStream(pStream);
    pStream->Release();

    if (!bmp || bmp->GetLastStatus() != Gdiplus::Ok) {
        delete bmp;
        return nullptr;
    }

    Gdiplus::Bitmap* scaled = new Gdiplus::Bitmap(cx, cy, PixelFormat32bppARGB);
    Gdiplus::Graphics g(scaled);
    g.DrawImage(bmp, 0, 0, cx, cy);
    delete bmp;

    HBITMAP hBmp = nullptr;
    scaled->GetHBITMAP(Gdiplus::Color(0, 0, 0, 0), &hBmp);
    delete scaled;

    return hBmp;
}

void InitImageList() {
    g_hImageList = ImageList_Create(g_nIconSize, g_nIconSize, ILC_COLOR32 | ILC_MASK, 1, 1);
    if (!g_hImageList) {
        AT_LOG_ERROR("Failed to create image list.");
        return;
    }

    HBITMAP hBmpCloseActive = LoadPngAsHBITMAP(g_hInstance, IDB_PNG_CLOSE_WINDOW_ACTIVE, g_nIconSize, g_nIconSize);
    if (hBmpCloseActive) {
        g_nImgCloseActiveInd = ImageList_Add(g_hImageList, hBmpCloseActive, nullptr);
        DeleteObject(hBmpCloseActive);
        AT_LOG_INFO("Close window active image index: %d", g_nImgCloseActiveInd);
    }

    HBITMAP hBmpCloseInactive =
        LoadPngAsHBITMAP(g_hInstance, IDB_PNG_CLOSE_WINDOW_INACTIVE, g_nIconSize, g_nIconSize);
    if (hBmpCloseInactive) {
        g_nImgCloseInactiveInd = ImageList_Add(g_hImageList, hBmpCloseInactive, nullptr);
        DeleteObject(hBmpCloseInactive);
        AT_LOG_INFO("Close window inactive image index: %d", g_nImgCloseInactiveInd);
    }
}
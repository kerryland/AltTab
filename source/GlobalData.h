#pragma once
#include "PreCompile.h"
#include <CommCtrl.h>
#include <memory>
#include <vector>
#include "log4cpp/CategoryStream.hh"

// ----------------------------------------------------------------------------
// Global declarations
// ----------------------------------------------------------------------------

#define TIMER_CHECK_ALT_KEYUP       1
#define TIMER_WINDOW_COUNT          2
#define TIMER_CHECK_FOR_UPDATES     3
#define TIMER_CUSTOM_TOOLTIP        4

#ifdef _DEBUG
#define TIMER_WINDOW_COUNT_ELAPSE   5000
#else
#define TIMER_WINDOW_COUNT_ELAPSE   100
#endif // _DEBUG

struct AltTabSettings;
struct AltTabWindowData;
using IsHungAppWindowFunc = BOOL(WINAPI*)(HWND);

extern HINSTANCE                            g_hInstance;
extern HWND                                 g_hMainWnd;              // AltTab main window handle
extern HWND                                 g_hSettingsWnd;          // AltTab settings window handle
extern HWND                                 g_hAltTabWnd;            // AltTab window handle
extern HWND                                 g_hSearchString;
extern HWND                                 g_hListView;
extern int                                  g_nLVHotItem;
extern AltTabSettings                       g_Settings;
extern IsHungAppWindowFunc                  g_pfnIsHungAppWindow;
extern HMENU                                g_hContextMenu;

extern std::shared_ptr<log4cpp::Category>   g_Logger;

extern std::vector<AltTabWindowData>        g_AltTabWindows;
extern AltTabWindowData                     g_AltBacktickWndInfo;    // source process for Alt+Backtick

extern bool                                 g_IsAltTab;
extern bool                                 g_IsAltBacktick;
extern bool                                 g_IsAltCtrlTab;
extern int                                  g_SelectedIndex;
extern std::wstring                         g_SearchString;
extern HIMAGELIST                           g_hImageList;
extern HIMAGELIST                           g_hLVImageList;
extern int                                  g_nImgCloseActiveInd;
extern int                                  g_nImgCloseInactiveInd;
#include "PreCompile.h"
#include "AltTabTooltip.h"
#include "Logger.h"
#include "GlobalData.h"
#include <CommCtrl.h>

namespace {
    HWND     g_hCustomToolTip = nullptr;
    TOOLINFO g_ToolInfo       = {};
    bool     g_TooltipVisible = false;
}

void CreateCustomToolTip() {
    AT_LOG_TRACE;

    g_hCustomToolTip = CreateWindowExW(
        WS_EX_TOPMOST,
        TOOLTIPS_CLASSW,
        nullptr,
        TTS_NOPREFIX | TTS_ALWAYSTIP,
        0,
        0,
        0,
        0,
        nullptr,
        nullptr,
        g_hInstance,
        nullptr);

    if (!g_hCustomToolTip) {
        AT_LOG_ERROR("Failed to create tooltip window.");
        return;
    }

    g_ToolInfo.cbSize      = sizeof(TOOLINFO);
    g_ToolInfo.uFlags      = TTF_TRACK;
    g_ToolInfo.hwnd        = nullptr;
    g_ToolInfo.hinst       = nullptr;
    g_ToolInfo.uId         = 0;
    g_ToolInfo.lpszText    = (LPWSTR)L"";
    g_ToolInfo.rect.left   = 0;
    g_ToolInfo.rect.top    = 0;
    g_ToolInfo.rect.right  = 0;
    g_ToolInfo.rect.bottom = 0;

    SendMessageW(g_hCustomToolTip, TTM_ADDTOOL, 0, (LPARAM)(LPTOOLINFO)&g_ToolInfo);
    SendMessageW(g_hCustomToolTip, TTM_SETMAXTIPWIDTH, 0, MAXINT);
    SendMessageW(g_hCustomToolTip, TTM_SETTIPBKCOLOR, RGB(255, 255, 0), 0);
}

void ShowCustomToolTip(const std::wstring& tooltipText, const int duration /*= 3000*/) {
    AT_LOG_TRACE;
    if (g_hCustomToolTip == nullptr)
        return;

    POINT pt;
    GetCursorPos(&pt);

    // Slight offset to avoid covering the mouse pointer
    ShowCustomToolTipAt(tooltipText, { pt.x + 12, pt.y + 12 }, duration);
}

void ShowCustomToolTipAt(const std::wstring& tooltipText, const POINT& pt, const int duration /*= 3000*/) {
    AT_LOG_TRACE;
    if (g_hCustomToolTip == nullptr)
        return;

    // Cancel any stale hide-timer for the previous tooltip.
    KillTimer(nullptr, TIMER_CUSTOM_TOOLTIP);

    g_ToolInfo.lpszText = (LPWSTR)(LPCWSTR)tooltipText.c_str();
    SendMessageW(g_hCustomToolTip, TTM_SETTOOLINFO, 0, (LPARAM)&g_ToolInfo);
    SendMessageW(g_hCustomToolTip, TTM_TRACKPOSITION, 0, (LPARAM)(DWORD)MAKELONG(pt.x, pt.y));
    SendMessageW(g_hCustomToolTip, TTM_TRACKACTIVATE, true, (LPARAM)(LPTOOLINFO)&g_ToolInfo);

    // A negative duration keeps the tooltip visible until explicitly hidden.
    SetTimer(nullptr, TIMER_CUSTOM_TOOLTIP, static_cast<UINT>(duration), HideCustomToolTip);
    g_TooltipVisible = true;
}

void CALLBACK HideCustomToolTip(HWND /*hWnd*/, UINT /*uMsg*/, UINT_PTR /*idEvent*/, DWORD /*dwTime*/) {
    AT_LOG_TRACE;
    KillTimer(nullptr, TIMER_CUSTOM_TOOLTIP);
    if (g_hCustomToolTip != nullptr) {
        SendMessageW(g_hCustomToolTip, TTM_TRACKACTIVATE, false, (LPARAM)(LPTOOLINFO)&g_ToolInfo);
    }
    g_TooltipVisible = false;
}

bool IsTooltipVisible() {
    return g_TooltipVisible;
}
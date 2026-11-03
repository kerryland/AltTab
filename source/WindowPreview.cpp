// WindowPreview.cpp : "selected window" preview panel.
//
// The panel sits to the right of the AltTab switcher and shows a *live* DWM
// thumbnail of the selected window - the same mechanism the Windows taskbar
// and its own Alt+Tab switcher use. This is both faithful to native behaviour
// and reliable: unlike PrintWindow (which returns partial/stale surfaces for
// hardware-accelerated and console windows), DWM renders the thumbnail from
// the window's own composition surface, so it works even while the window is
// occluded by the switcher.
//
// DWM thumbnails require the destination to be a top-level window, so the
// panel is a small top-level popup rather than a child control. DWM does the
// compositing, so the UI thread does essentially no per-frame work.

#include "PreCompile.h"
#include "WindowPreview.h"
#include "AltTabWindow.h"
#include "AltTabSettings.h"
#include "GlobalData.h"
#include "Logger.h"
#include "Utils.h"
#include <dwmapi.h>

#pragma comment(lib, "dwmapi.lib")

namespace {

constexpr wchar_t PREVIEW_CLASS_NAME[] = L"__AltTab_PreviewCls__";
constexpr int     kPreviewPanelMargin  = 8;   // space around the thumbnail
constexpr int     kPreviewBorderWidth  = 1;   // panel frame thickness
constexpr int     kPreviewTitleHeight  = 20;  // bottom caption strip

HWND        g_previewWnd       = nullptr;
HTHUMBNAIL  g_thumbnail        = nullptr;
HWND        g_thumbSource      = nullptr;
HFONT       g_previewTitleFont = nullptr;
int         g_sourceWidth      = 0;   // source window size (for aspect-fit)
int         g_sourceHeight     = 0;

// The client-space area available for the thumbnail: inside the panel
// border/margin and above the title strip.
RECT GetAvailableThumbArea() {
    RECT rcClient;
    GetClientRect(g_previewWnd, &rcClient);

    RECT rc = rcClient;
    InflateRect(&rc, -(kPreviewBorderWidth + kPreviewPanelMargin), -(kPreviewBorderWidth + kPreviewPanelMargin));
    rc.bottom -= kPreviewTitleHeight;
    return rc;
}

// Determine the source window's shape for aspect-fitting the thumbnail.
//
// A minimized window reports an iconized/off-screen rect from GetWindowRect
// and DWMWA_EXTENDED_FRAME_BOUNDS, but the DWM thumbnail shows the window's
// restored (last rendered) frame - so use the restored placement's size in that
// case. Getting this wrong stretches the thumbnail (squashed preview).
void GetSourceWindowSize(HWND hWnd, int& width, int& height) {
    width  = 0;
    height = 0;

    if (IsIconic(hWnd)) {
        WINDOWPLACEMENT wp = {};
        wp.length = sizeof(wp);
        if (GetWindowPlacement(hWnd, &wp)) {
            width  = wp.rcNormalPosition.right - wp.rcNormalPosition.left;
            height = wp.rcNormalPosition.bottom - wp.rcNormalPosition.top;
            if (width > 0 && height > 0) {
                return;
            }
        }
    }

    // Prefer the visible frame (what DWM actually thumbnails) over the window
    // rect, which includes the invisible resize borders.
    RECT rc = {};
    if (SUCCEEDED(DwmGetWindowAttribute(hWnd, DWMWA_EXTENDED_FRAME_BOUNDS, &rc, sizeof(rc)))
        && rc.right > rc.left && rc.bottom > rc.top) {
        width  = rc.right - rc.left;
        height = rc.bottom - rc.top;
        return;
    }

    if (GetWindowRect(hWnd, &rc) && rc.right > rc.left && rc.bottom > rc.top) {
        width  = rc.right - rc.left;
        height = rc.bottom - rc.top;
    }
}

void UpdateThumbnailProperties() {
    if (g_thumbnail == nullptr || g_previewWnd == nullptr) {
        return;
    }

    const RECT area = GetAvailableThumbArea();
    const int areaW = area.right - area.left;
    const int areaH = area.bottom - area.top;
    if (areaW <= 0 || areaH <= 0) {
        return;
    }

    // Fit the destination rectangle to the source's aspect ratio so DWM does
    // not stretch (and distort) windows whose shape differs from the panel.
    RECT dest = area;
    if (g_sourceWidth > 0 && g_sourceHeight > 0) {
        const double scale = min((double)areaW / g_sourceWidth, (double)areaH / g_sourceHeight);
        const int drawW = max(1, (int)(g_sourceWidth * scale));
        const int drawH = max(1, (int)(g_sourceHeight * scale));
        dest.left   = area.left + (areaW - drawW) / 2;
        dest.top    = area.top + (areaH - drawH) / 2;
        dest.right  = dest.left + drawW;
        dest.bottom = dest.top + drawH;
    }

    DWM_THUMBNAIL_PROPERTIES props = {};
    props.dwFlags               = DWM_TNP_RECTDESTINATION | DWM_TNP_VISIBLE | DWM_TNP_OPACITY;
    props.rcDestination         = dest;
    props.opacity               = 255;
    props.fVisible              = TRUE;
    props.fSourceClientAreaOnly = FALSE;    // include the window frame, like native Alt+Tab
    DwmUpdateThumbnailProperties(g_thumbnail, &props);
}

void UnregisterThumbnail() {
    if (g_thumbnail != nullptr) {
        DwmUnregisterThumbnail(g_thumbnail);
        g_thumbnail = nullptr;
    }
    g_thumbSource  = nullptr;
    g_sourceWidth  = 0;
    g_sourceHeight = 0;
}

LRESULT CALLBACK PreviewWndProc(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
    switch (uMsg) {
    case WM_ERASEBKGND:
        // WM_PAINT fills the whole client area.
        return TRUE;

    case WM_MOUSEACTIVATE:
        // Never steal activation from the switcher; the click is handled below.
        return MA_NOACTIVATE;

    case WM_LBUTTONUP:
        // Clicking the preview activates the selected window (native behavior).
        DestroyAltTabWindow(true);
        return 0;

    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(hWnd, &ps);

        RECT rcClient;
        GetClientRect(hWnd, &rcClient);

        // Panel background.
        HBRUSH hBg = CreateSolidBrush(RGB(24, 24, 26));
        FillRect(hdc, &rcClient, hBg);
        DeleteObject(hBg);

        // Thin frame.
        HPEN hPen = CreatePen(PS_SOLID, 1, RGB(90, 90, 90));
        HGDIOBJ hOldPen = SelectObject(hdc, hPen);
        HGDIOBJ hOldBrush = SelectObject(hdc, GetStockObject(NULL_BRUSH));
        Rectangle(hdc, rcClient.left, rcClient.top, rcClient.right, rcClient.bottom);
        SelectObject(hdc, hOldBrush);
        SelectObject(hdc, hOldPen);
        DeleteObject(hPen);

        RECT rcThumb = GetAvailableThumbArea();

        // DWM composites the thumbnail on top of this area; only show the hint
        // when there is no thumbnail to draw.
        if (g_thumbnail == nullptr) {
            SetBkMode(hdc, TRANSPARENT);
            SetTextColor(hdc, RGB(140, 140, 140));
            RECT rcEmpty = rcThumb;
            DrawTextW(hdc, L"No preview", -1, &rcEmpty, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
        }

        // Title caption of the currently selected window.
        if (g_SelectedIndex >= 0 && g_SelectedIndex < (int)g_AltTabWindows.size()) {
            RECT rcTitle = rcClient;
            rcTitle.top = rcThumb.bottom;
            SetBkMode(hdc, TRANSPARENT);
            SetTextColor(hdc, RGB(230, 230, 230));
            HFONT hOldFont = (HFONT)SelectObject(hdc, g_previewTitleFont);
            DrawTextW(
                hdc,
                g_AltTabWindows[g_SelectedIndex].Title.c_str(),
                -1,
                &rcTitle,
                DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);
            SelectObject(hdc, hOldFont);
        }

        EndPaint(hWnd, &ps);
        return 0;
    }
    }
    return DefWindowProcW(hWnd, uMsg, wParam, lParam);
}

} // namespace

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------

bool IsWindowPreviewEnabled() {
    return g_Settings.ShowWindowPreview;
}

int GetWindowPreviewWidth(int listWindowWidth) {
    if (!g_Settings.ShowWindowPreview) {
        return 0;
    }

    // Keep the whole AltTab window (plus the panel) on the primary screen.
    const int screenWidth = GetSystemMetrics(SM_CXSCREEN);
    const int maxPreview  = max(0, screenWidth - listWindowWidth);
    if (maxPreview <= 0) {
        return 0;
    }

    const int previewWidth = max(120, (int)(listWindowWidth * 0.45));
    return min(previewWidth, maxPreview);
}

void CreateWindowPreview() {
    if (g_previewWnd != nullptr) {
        return;    // reused across switcher instances
    }

    WNDCLASS wc = {};
    wc.lpfnWndProc   = PreviewWndProc;
    wc.hInstance     = g_hInstance;
    wc.lpszClassName = PREVIEW_CLASS_NAME;
    RegisterClass(&wc);

    g_previewWnd = CreateWindowExW(
        WS_EX_TOOLWINDOW | WS_EX_TOPMOST | WS_EX_NOACTIVATE,
        PREVIEW_CLASS_NAME,
        L"",
        WS_POPUP,
        0,
        0,
        0,
        0,
        nullptr,
        nullptr,
        g_hInstance,
        nullptr);
    if (g_previewWnd == nullptr) {
        AT_LOG_ERROR("Failed to create window preview panel.");
        return;
    }

    HDC hdc = GetDC(g_previewWnd);
    g_previewTitleFont = CreateFontW(
        -MulDiv(12, GetDeviceCaps(hdc, LOGPIXELSY), 72),
        0,
        0,
        0,
        FW_NORMAL,
        FALSE,
        FALSE,
        FALSE,
        DEFAULT_CHARSET,
        OUT_DEFAULT_PRECIS,
        CLIP_DEFAULT_PRECIS,
        CLEARTYPE_QUALITY,
        DEFAULT_PITCH | FF_SWISS,
        g_Settings.LVFontName.c_str());
    ReleaseDC(g_previewWnd, hdc);

    AT_LOG_INFO("Window preview panel created: [%#08X]", g_previewWnd);
}

void LayoutWindowPreview(HWND hAltTabWnd, int listWindowWidth) {
    if (!g_Settings.ShowWindowPreview || g_previewWnd == nullptr || hAltTabWnd == nullptr) {
        return;
    }

    RECT rcClient;
    if (!GetClientRect(hAltTabWnd, &rcClient)) {
        return;
    }

    // Anchor the panel to the right edge of the switcher's client area.
    POINT ptTopRight = { rcClient.right, rcClient.top };
    ClientToScreen(hAltTabWnd, &ptTopRight);

    const int width  = GetWindowPreviewWidth(listWindowWidth);
    // Always use the configured window height rather than the (possibly much
    // shorter) content-sized switcher, so a low window count doesn't shrink the
    // preview. The switcher's top is already centred for the full height, so
    // the panel ends up vertically centred too. `max` guards the rare case where
    // the switcher is taller than the configured height.
    const int height = max(g_Settings.WindowHeight, rcClient.bottom - rcClient.top);
    if (width <= 0 || height <= 0) {
        return;
    }

    SetWindowPos(
        g_previewWnd,
        HWND_TOPMOST,
        ptTopRight.x + 1,
        ptTopRight.y,
        width,
        height,
        SWP_NOACTIVATE | SWP_NOOWNERZORDER);

    UpdateThumbnailProperties();
    InvalidateRect(g_previewWnd, nullptr, TRUE);
}

void ShowWindowPreview(bool visible) {
    if (g_previewWnd == nullptr) {
        return;
    }

    if (!visible || !g_Settings.ShowWindowPreview) {
        ShowWindow(g_previewWnd, SW_HIDE);
        return;
    }

    SetWindowPos(
        g_previewWnd,
        HWND_TOPMOST,
        0,
        0,
        0,
        0,
        SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE | SWP_SHOWWINDOW);
    UpdateThumbnailProperties();
}

void RequestWindowPreview() {
    if (!g_Settings.ShowWindowPreview || g_previewWnd == nullptr) {
        return;
    }
    if (g_SelectedIndex < 0 || g_SelectedIndex >= (int)g_AltTabWindows.size()) {
        return;
    }

    const HWND hTarget = g_AltTabWindows[g_SelectedIndex].hWnd;
    if (hTarget == nullptr) {
        return;
    }

    if (hTarget != g_thumbSource) {
        UnregisterThumbnail();

        HTHUMBNAIL thumbnail = nullptr;
        const HRESULT hr = DwmRegisterThumbnail(g_previewWnd, hTarget, &thumbnail);
        if (FAILED(hr) || thumbnail == nullptr) {
            AT_LOG_WARN(
                "DwmRegisterThumbnail failed (hr = %#08X) for window [%#08X]",
                (unsigned)hr,
                (unsigned)(UINT_PTR)hTarget);
            InvalidateRect(g_previewWnd, nullptr, TRUE);
            return;
        }
        g_thumbnail   = thumbnail;
        g_thumbSource = hTarget;
    }

    // Refresh the source size on every request: a minimized window reports a
    // bogus rect (handled in GetSourceWindowSize) and the window may be resized
    // while the switcher stays open. A stale size stretches the thumbnail.
    GetSourceWindowSize(hTarget, g_sourceWidth, g_sourceHeight);
    UpdateThumbnailProperties();
    InvalidateRect(g_previewWnd, nullptr, TRUE);
}

void WindowPreviewReset() {
    UnregisterThumbnail();
    if (g_previewWnd != nullptr) {
        ShowWindow(g_previewWnd, SW_HIDE);
    }
}
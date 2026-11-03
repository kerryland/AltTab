#pragma once

#include <windows.h>

/*!
 * \brief Whether the window preview panel is enabled (ShowWindowPreview=1).
 */
bool IsWindowPreviewEnabled();

/*!
 * \brief Extra width (in px) the preview panel adds to the AltTab window, or 0
 *        when the preview is disabled. Clamped so the whole window stays on
 *        the primary screen.
 */
int GetWindowPreviewWidth(int listWindowWidth);

/*!
 * \brief Create the preview window (a top-level popup, which is required for
 *        DWM thumbnails). Kept alive across switcher instances; call from the
 *        AltTab window's WM_CREATE. Must run on the UI thread.
 */
void CreateWindowPreview();

/*!
 * \brief Position the preview window alongside the AltTab window (to its
 *        right) and refresh the thumbnail destination rectangle. Call whenever
 *        the AltTab window has been (re)positioned.
 */
void LayoutWindowPreview(HWND hAltTabWnd, int listWindowWidth);

/*!
 * \brief Show or hide the preview window (shown with the switcher).
 */
void ShowWindowPreview(bool visible);

/*!
 * \brief Point the live DWM thumbnail at the currently selected window.
 *        Cheap: DwmRegisterThumbnail/DwmUpdateThumbnailProperties only; DWM
 *        composites the (live) content, so there is no capture work to do.
 */
void RequestWindowPreview();

/*!
 * \brief Unregister the thumbnail and hide the preview window (the AltTab
 *        window is closing).
 */
void WindowPreviewReset();
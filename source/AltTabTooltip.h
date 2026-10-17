#pragma once

#include <string>
#include <wtypes.h>

/*!
 * \brief Create the ToolTip control used by the whole application.
 *
 * Must be called once before any ShowCustomToolTip usage.
 */
void CreateCustomToolTip();

/*!
 * \brief Show a custom tooltip near the current cursor position.
 *
 * \param tooltipText Text to display.
 * \param duration    Milliseconds the tooltip stays visible, or -1 to keep it
 *                    visible until HideCustomToolTip() is called.
 */
void ShowCustomToolTip(const std::wstring& tooltipText, int duration = 3000);

/*!
 * \brief Show a custom tooltip at the given screen position.
 */
void ShowCustomToolTipAt(const std::wstring& tooltipText, const POINT& pt, int duration = 3000);

/*!
 * \brief Hide the custom tooltip (also used as the timer callback).
 */
void CALLBACK HideCustomToolTip(HWND hWnd = nullptr, UINT uMsg = 0, UINT_PTR idEvent = 0, DWORD dwTime = 0);

/*!
 * \brief Whether a custom tooltip is currently visible (used to avoid
 *        flickering when the same tooltip would be re-shown).
 */
bool IsTooltipVisible();
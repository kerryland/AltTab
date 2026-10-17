#pragma once

#include "resource.h"
#include <string>

/*!
 * @brief General settings of the application.
 * When the application starts, it checks the current user privileges.
 */
struct GeneralSettings {
    bool IsProcessElevated; // Is the application running with elevated privileges
    bool IsTaskElevated;    // Is the application set to run elevated (Run with highest privileges in the task options)
    bool IsRunAtStartup;    // Is the application set to run at startup
};

extern GeneralSettings g_GeneralSettings;
extern HHOOK           g_KeyboardHook;

/*!
 * \brief Query the current privileges / run-at-startup state.
 */
GeneralSettings GetGeneralSettings();

LRESULT CALLBACK LLKeyboardProc(int nCode, WPARAM wp, LPARAM lp);

LRESULT CALLBACK MainWndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam);

void RestartAltTab();
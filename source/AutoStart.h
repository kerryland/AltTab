#pragma once

#include <string>

/*!
 * \brief Enable or disable AltTab's "Run at startup" behaviour.
 *
 * Run-at-startup is implemented with a Task Scheduler task. A legacy HKCU
 * Software\\Microsoft\\Windows\\CurrentVersion\\Run value is removed if present.
 *
 * \param runAtStartup        true to run AltTab at logon, false to remove it.
 * \param withHighestPrivileges  true to run the task elevated.
 * \return true on success.
 */
bool RunAtStartup(bool runAtStartup, bool withHighestPrivileges);

/*!
 * \brief Check whether AltTab is configured to run at startup.
 */
bool IsRunAtStartup();

/*!
 * \brief Remove the legacy "Run" registry value (Task Scheduler is used now).
 */
void RemoveStartupFromRegistry();

bool CreateAutoStartTask(bool runElevated);
bool DeleteAutoStartTask();
bool IsAutoStartTaskActive();
bool IsTaskRunWithHighestPrivileges();
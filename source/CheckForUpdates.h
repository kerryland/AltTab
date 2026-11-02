#include <chrono>

/*!
 * @brief Check for updates
 * @param quiteMode Whether to run in quiet mode or not. If true, it will not show any message boxes or dialogs.
 */
void CheckForUpdates(const bool quiteMode = false);

/*!
 * @brief Run an update check on a single-flight background thread (replaces
 *        the old fire-and-forget detach() pattern). Safe to call from any
 *        thread; a check already in progress makes it a no-op.
 */
void StartUpdateCheck(bool quiteMode);

/*!
 * @brief Stop a running background update check and join its thread. Called
 *        once from WinMain at exit so the worker cannot outlive the CRT
 *        teardown (which previously crashed in EnterCriticalSection).
 */
void StopUpdateCheck();

std::chrono::system_clock::time_point ReadLastCheckForUpdatesTS();

void WriteCheckForUpdatesTS(const std::chrono::system_clock::time_point& timestamp);

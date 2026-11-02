#pragma once

#include <string>
#include <windows.h>

/*!
 * \brief Open the "Similar Process Groups" manager dialog (modal).
 *
 * The dialog edits a structured copy of the groups (a list of groups, each a
 * list of process names) so the user never has to type the '|' / '/' separator
 * syntax the settings file stores internally.
 *
 * \param hOwner       Owner window (e.g. the settings dialog); disabled while
 *                     this dialog is up.
 * \param currentValue Current pipe('/')/group('|') separated value.
 * \param[out] newValue Serialized groups string on OK; left unchanged on Cancel.
 *
 * \return true if the user pressed OK (newValue holds the edited groups).
 */
bool EditSimilarProcessGroups(HWND hOwner, const std::wstring& currentValue, std::wstring& newValue);
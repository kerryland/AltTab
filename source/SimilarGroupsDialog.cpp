#include "PreCompile.h"
#include "SimilarGroupsDialog.h"
#include "Resource.h"
#include "ProcessExplorer.h"
#include "Utils.h"
#include "Logger.h"
#include "GlobalData.h"
#include <algorithm>
#include <CommCtrl.h>
#include <vector>

namespace {

// The dialog edits these in memory; nothing is persisted until OK.
std::vector<std::vector<std::wstring>> g_groups;   // group index -> member process names
std::vector<std::wstring>              g_available; // processes not assigned to any group
std::wstring                           g_result;    // serialized groups on OK

bool CaseInsensitiveLess(const std::wstring& a, const std::wstring& b) {
    return ToLower(a) < ToLower(b);
}

void SortMembers(std::vector<std::wstring>& members) {
    std::sort(members.begin(), members.end(), CaseInsensitiveLess);
}

bool IsMemberOfAnyGroup(const std::wstring& name) {
    return std::any_of(g_groups.begin(), g_groups.end(), [&](const std::vector<std::wstring>& group) {
        return std::any_of(group.begin(), group.end(), [&](const std::wstring& member) {
            return EqualsIgnoreCase(member, name);
        });
    });
}

// Parse the pipe('|') group / slash('/') member value into g_groups.
void ParseGroups(const std::wstring& value) {
    g_groups.clear();
    for (const std::wstring& groupStr : Split(value, L"|")) {
        std::vector<std::wstring> members;
        for (const std::wstring& member : Split(groupStr, L"/")) {
            const bool duplicate = std::any_of(members.begin(), members.end(), [&](const std::wstring& m) {
                return EqualsIgnoreCase(m, member);
            });
            if (!member.empty() && !duplicate) {
                members.push_back(member);
            }
        }
        if (!members.empty()) {
            SortMembers(members);
            g_groups.push_back(std::move(members));
        }
    }
}

// Serialize g_groups back to the pipe/slash value the settings file uses.
std::wstring SerializeGroups() {
    std::wstring result;
    for (const auto& group : g_groups) {
        if (group.empty()) {
            continue;
        }
        if (!result.empty()) {
            result += L'|';
        }
        for (size_t i = 0; i < group.size(); ++i) {
            if (i > 0) {
                result += L'/';
            }
            result += group[i];
        }
    }
    return result;
}

// Build the pool of processes that are not assigned to any group yet: every
// process with a switchable window right now, plus every process already
// configured (so a member that was removed can be re-added even when it is not
// currently running).
void BuildAvailableProcesses() {
    g_available.clear();

    std::vector<std::wstring> candidates;
    for (const auto& window : GetAltTabWindows()) {
        if (!window.ProcessName.empty()) {
            candidates.push_back(window.ProcessName);
        }
    }
    for (const auto& group : g_groups) {
        for (const auto& member : group) {
            candidates.push_back(member);
        }
    }

    std::vector<std::wstring> unique;
    for (const std::wstring& candidate : candidates) {
        const bool present = std::any_of(unique.begin(), unique.end(), [&](const std::wstring& u) {
            return EqualsIgnoreCase(u, candidate);
        });
        if (!present) {
            unique.push_back(candidate);
        }
    }

    for (const std::wstring& candidate : unique) {
        if (!IsMemberOfAnyGroup(candidate)) {
            g_available.push_back(candidate);
        }
    }
    std::sort(g_available.begin(), g_available.end(), CaseInsensitiveLess);
}

// ----------------------------------------------------------------------------
// ListView helpers
// ----------------------------------------------------------------------------

int GetSelectedRow(HWND hListView) {
    return (int)SendMessageW(hListView, LVM_GETNEXTITEM, (WPARAM)-1, LVNI_SELECTED);
}

void InitListView(HWND hListView, const wchar_t* header) {
    ListView_SetExtendedListViewStyle(hListView, LVS_EX_FULLROWSELECT | LVS_EX_DOUBLEBUFFER);
    RECT rc = {};
    GetClientRect(hListView, &rc);
    LVCOLUMN column = {};
    column.mask    = LVCF_TEXT | LVCF_WIDTH;
    column.cx      = max(rc.right - rc.left - 4, 50);
    column.pszText = const_cast<wchar_t*>(header);
    ListView_InsertColumn(hListView, 0, &column);
}

void RefreshGroupMembers(HWND hDlg);

// Rebuild the groups rows, restoring the previously selected group when possible.
void RefreshGroupsList(HWND hDlg) {
    HWND hListView = GetDlgItem(hDlg, IDC_LISTV_GROUPS);
    const int previous = GetSelectedRow(hListView);
    ListView_DeleteAllItems(hListView);

    for (int i = 0; i < (int)g_groups.size(); ++i) {
        std::wstring label;
        for (size_t j = 0; j < g_groups[i].size(); ++j) {
            if (j > 0) {
                label += L", ";
            }
            label += g_groups[i][j];
        }
        LVITEM item      = {};
        item.mask        = LVIF_TEXT | LVIF_PARAM;
        item.iItem       = i;
        item.pszText     = const_cast<wchar_t*>(label.c_str());
        item.lParam      = i;
        ListView_InsertItem(hListView, &item);
    }

    if (!g_groups.empty()) {
        const int target = (previous >= 0 && previous < (int)g_groups.size()) ? previous : 0;
        ListView_SetItemState(hListView, target, LVIS_SELECTED | LVIS_FOCUSED, LVIS_SELECTED | LVIS_FOCUSED);
    }
}

// Rebuild the members of the currently selected group.
void RefreshGroupMembers(HWND hDlg) {
    HWND hListView = GetDlgItem(hDlg, IDC_LISTV_GROUP_MEMBERS);
    ListView_DeleteAllItems(hListView);

    const int group = GetSelectedRow(GetDlgItem(hDlg, IDC_LISTV_GROUPS));
    if (group >= 0 && group < (int)g_groups.size()) {
        for (int i = 0; i < (int)g_groups[group].size(); ++i) {
            LVITEM item  = {};
            item.mask    = LVIF_TEXT;
            item.iItem   = i;
            item.pszText = const_cast<wchar_t*>(g_groups[group][i].c_str());
            ListView_InsertItem(hListView, &item);
        }
    }
}

void RefreshAvailableList(HWND hDlg) {
    HWND hListView = GetDlgItem(hDlg, IDC_LISTV_AVAILABLE_PROCESSES);
    ListView_DeleteAllItems(hListView);

    for (int i = 0; i < (int)g_available.size(); ++i) {
        LVITEM item  = {};
        item.mask    = LVIF_TEXT;
        item.iItem   = i;
        item.pszText = const_cast<wchar_t*>(g_available[i].c_str());
        ListView_InsertItem(hListView, &item);
    }
}

void UpdateButtons(HWND hDlg) {
    const bool hasGroup  = GetSelectedRow(GetDlgItem(hDlg, IDC_LISTV_GROUPS)) >= 0;
    const bool hasMember = GetSelectedRow(GetDlgItem(hDlg, IDC_LISTV_GROUP_MEMBERS)) >= 0;
    const bool hasAvail  = GetSelectedRow(GetDlgItem(hDlg, IDC_LISTV_AVAILABLE_PROCESSES)) >= 0;

    EnableWindow(GetDlgItem(hDlg, IDC_BUTTON_DELETE_GROUP), hasGroup);
    EnableWindow(GetDlgItem(hDlg, IDC_BUTTON_REMOVE_PROCESS), hasMember);
    EnableWindow(GetDlgItem(hDlg, IDC_BUTTON_ADD_PROCESS), hasAvail);
}

void RefreshAll(HWND hDlg) {
    RefreshGroupsList(hDlg);
    RefreshGroupMembers(hDlg);
    RefreshAvailableList(hDlg);
    UpdateButtons(hDlg);
}

void SelectGroup(HWND hDlg, int group) {
    HWND hListView = GetDlgItem(hDlg, IDC_LISTV_GROUPS);
    ListView_SetItemState(hListView, group, LVIS_SELECTED | LVIS_FOCUSED, LVIS_SELECTED | LVIS_FOCUSED);
}

// ----------------------------------------------------------------------------
// Actions
// ----------------------------------------------------------------------------

// Move the selected "available" process into the selected group; creates a new
// group when none is selected.
void AddSelectedProcess(HWND hDlg) {
    const int available = GetSelectedRow(GetDlgItem(hDlg, IDC_LISTV_AVAILABLE_PROCESSES));
    if (available < 0 || available >= (int)g_available.size()) {
        return;
    }

    const std::wstring name = g_available[available];
    int group = GetSelectedRow(GetDlgItem(hDlg, IDC_LISTV_GROUPS));
    if (group < 0) {
        g_groups.push_back({});
        group = (int)g_groups.size() - 1;
    }

    g_groups[group].push_back(name);
    SortMembers(g_groups[group]);
    g_available.erase(g_available.begin() + available);

    RefreshAll(hDlg);
    SelectGroup(hDlg, group);
}

// Move the selected member out of the selected group (deleting the group when
// it becomes empty).
void RemoveSelectedMember(HWND hDlg) {
    const int group = GetSelectedRow(GetDlgItem(hDlg, IDC_LISTV_GROUPS));
    const int member = GetSelectedRow(GetDlgItem(hDlg, IDC_LISTV_GROUP_MEMBERS));
    if (group < 0 || group >= (int)g_groups.size()) {
        return;
    }
    if (member < 0 || member >= (int)g_groups[group].size()) {
        return;
    }

    g_available.push_back(g_groups[group][member]);
    std::sort(g_available.begin(), g_available.end(), CaseInsensitiveLess);
    g_groups[group].erase(g_groups[group].begin() + member);

    RefreshAll(hDlg);

    if (g_groups[group].empty()) {
        const int target = group < (int)g_groups.size() ? group : (int)g_groups.size() - 1;
        g_groups.erase(g_groups.begin() + group);
        RefreshAll(hDlg);
        if (target >= 0) {
            SelectGroup(hDlg, target);
        }
    } else {
        SelectGroup(hDlg, group);
    }
}

// Delete the selected group entirely (its members return to the available pool).
void DeleteSelectedGroup(HWND hDlg) {
    const int group = GetSelectedRow(GetDlgItem(hDlg, IDC_LISTV_GROUPS));
    if (group < 0 || group >= (int)g_groups.size()) {
        return;
    }

    for (const auto& member : g_groups[group]) {
        g_available.push_back(member);
    }
    g_groups.erase(g_groups.begin() + group);
    std::sort(g_available.begin(), g_available.end(), CaseInsensitiveLess);

    RefreshAll(hDlg);

    const int target = group < (int)g_groups.size() ? group : (int)g_groups.size() - 1;
    if (target >= 0) {
        SelectGroup(hDlg, target);
    }
}

} // namespace

// ----------------------------------------------------------------------------
// Dialog
// ----------------------------------------------------------------------------

INT_PTR CALLBACK ATSimilarGroupsDlgProc(HWND hDlg, UINT message, WPARAM wParam, LPARAM lParam) {
    switch (message) {
    case WM_INITDIALOG: {
        HICON hIcon = LoadIcon(g_hInstance, MAKEINTRESOURCE(IDI_ALTTAB));
        SendMessageW(hDlg, WM_SETICON, ICON_SMALL, (LPARAM)hIcon);
        SendMessageW(hDlg, WM_SETICON, ICON_BIG, (LPARAM)hIcon);

        InitListView(GetDlgItem(hDlg, IDC_LISTV_GROUPS), L"Processes");
        InitListView(GetDlgItem(hDlg, IDC_LISTV_GROUP_MEMBERS), L"Members");
        InitListView(GetDlgItem(hDlg, IDC_LISTV_AVAILABLE_PROCESSES), L"Process Name");
        RefreshAll(hDlg);
        return (INT_PTR)TRUE;
    }

    case WM_NOTIFY: {
        const NMHDR* pnmhdr = reinterpret_cast<NMHDR*>(lParam);
        if (pnmhdr->code == LVN_ITEMCHANGED) {
            const auto* pnmListView = reinterpret_cast<const NMLISTVIEW*>(lParam);
            const bool selectionChanged =
                (pnmListView->uNewState & LVIS_SELECTED) != (pnmListView->uOldState & LVIS_SELECTED);
            if (selectionChanged) {
                if (pnmhdr->idFrom == IDC_LISTV_GROUPS) {
                    RefreshGroupMembers(hDlg);
                }
                UpdateButtons(hDlg);
            }
        } else if (pnmhdr->code == LVN_KEYDOWN) {
            // Delete key: drop the selected group / member without hunting for
            // the on-screen button.
            const auto* pnmKey = reinterpret_cast<const NMLVKEYDOWN*>(lParam);
            if (pnmKey->wVKey == VK_DELETE) {
                if (pnmhdr->idFrom == IDC_LISTV_GROUPS) {
                    DeleteSelectedGroup(hDlg);
                } else if (pnmhdr->idFrom == IDC_LISTV_GROUP_MEMBERS) {
                    RemoveSelectedMember(hDlg);
                }
            }
        } else if (pnmhdr->code == NM_RCLICK && pnmhdr->idFrom == IDC_LISTV_GROUPS) {
            const int group = GetSelectedRow(GetDlgItem(hDlg, IDC_LISTV_GROUPS));
            if (group >= 0 && group < (int)g_groups.size()) {
                HMENU hMenu = CreatePopupMenu();
                if (hMenu) {
                    AppendMenuW(hMenu, MF_STRING, 1, L"&Delete Group");
                    POINT pt;
                    GetCursorPos(&pt);
                    if (TrackPopupMenu(hMenu, TPM_RIGHTBUTTON | TPM_RETURNCMD, pt.x, pt.y, 0, hDlg, nullptr) == 1) {
                        DeleteSelectedGroup(hDlg);
                    }
                    DestroyMenu(hMenu);
                }
            }
        }
        return 0;
    }

    case WM_COMMAND: {
        switch (LOWORD(wParam)) {
        case IDC_BUTTON_ADD_PROCESS:
            AddSelectedProcess(hDlg);
            return (INT_PTR)TRUE;

        case IDC_BUTTON_REMOVE_PROCESS:
            RemoveSelectedMember(hDlg);
            return (INT_PTR)TRUE;

        case IDC_BUTTON_ADD_GROUP:
            // Create an empty group (members are added with "Add ->"); groups
            // are held in memory until OK, so Cancel discards it.
            g_groups.push_back({});
            RefreshAll(hDlg);
            SelectGroup(hDlg, (int)g_groups.size() - 1);
            return (INT_PTR)TRUE;

        case IDC_BUTTON_DELETE_GROUP:
            DeleteSelectedGroup(hDlg);
            return (INT_PTR)TRUE;

        case IDC_BUTTON_REFRESH_PROCESSES:
            // Re-scan for processes that started while the dialog was open;
            // the in-progress group edits are kept.
            BuildAvailableProcesses();
            RefreshAvailableList(hDlg);
            UpdateButtons(hDlg);
            return (INT_PTR)TRUE;

        case IDOK:
            g_result = SerializeGroups();
            EndDialog(hDlg, IDOK);
            return (INT_PTR)TRUE;

        case IDCANCEL:
            EndDialog(hDlg, IDCANCEL);
            return (INT_PTR)TRUE;
        }
    }
    break;

    case WM_DESTROY: {
        DestroyIcon((HICON)SendMessageW(hDlg, WM_GETICON, ICON_SMALL, 0));
        DestroyIcon((HICON)SendMessageW(hDlg, WM_GETICON, ICON_BIG, 0));
    }
    break;
    }

    return (INT_PTR)FALSE;
}

bool EditSimilarProcessGroups(HWND hOwner, const std::wstring& currentValue, std::wstring& newValue) {
    ParseGroups(currentValue);
    BuildAvailableProcesses();
    g_result.clear();

    const INT_PTR result = DialogBoxW(g_hInstance, MAKEINTRESOURCE(IDD_SIMILARGROUPS), hOwner, ATSimilarGroupsDlgProc);
    if (result == IDOK) {
        newValue = g_result;
        return true;
    }
    return false;
}
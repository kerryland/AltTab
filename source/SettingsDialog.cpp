#include "PreCompile.h"
#include "SettingsDialog.h"
#include "AltTabSettings.h"
#include "GlobalData.h"
#include "Tooltips.h"
#include "Resource.h"
#include "Logger.h"
#include "Utils.h"
#include "version.h"
#include <CommCtrl.h>
#include <windowsx.h>

HWND g_hSettingsWnd = nullptr;

namespace {

HWND g_hToolTip     = nullptr;

#define BOOL_TO_CSTR(b) ((b) ? "true" : "false")

#define ADD_TOOLTIP(id, text)                                                                                          \
    toolInfo.hwnd = GetDlgItem(hDlg, id);                                                                              \
    toolInfo.lpszText = (LPWSTR)text;                                                                                  \
    GetClientRect(toolInfo.hwnd, &toolInfo.rect);                                                                      \
    SendMessageW(g_hToolTip, TTM_ADDTOOL, 0, reinterpret_cast<LPARAM>(&toolInfo));

/*!
 * \brief Add tooltips to the settings dialog controls.
 */
void AddTooltips(HWND hDlg) {
    g_hToolTip = CreateWindowEx(
        0,
        TOOLTIPS_CLASS,
        nullptr,
        WS_POPUP | TTS_NOPREFIX | TTS_ALWAYSTIP,
        CW_USEDEFAULT,
        CW_USEDEFAULT,
        CW_USEDEFAULT,
        CW_USEDEFAULT,
        hDlg,
        nullptr,
        g_hInstance,
        nullptr);

    TOOLINFO toolInfo = {};
    toolInfo.cbSize = sizeof(toolInfo);
    toolInfo.hwnd   = hDlg;
    toolInfo.uFlags = TTF_SUBCLASS;

    SendMessageW(g_hToolTip, TTM_SETMAXTIPWIDTH, 0, MAXINT);

    ADD_TOOLTIP(IDC_CHECK_ALT_TAB                    , TT_HOTKEY_ALT_TAB            );
    ADD_TOOLTIP(IDC_CHECK_ALT_BACKTICK               , TT_HOTKEY_ALT_BACKTICK       );
    ADD_TOOLTIP(IDC_CHECK_ALT_CTRL_TAB               , TT_HOTKEY_ALT_CTRL_TAB       );
    ADD_TOOLTIP(IDC_EDIT_SETTINGS_FILEPATH           , TT_SETTINGS_FILEPATH         );
    ADD_TOOLTIP(IDC_EDIT_SS_BANNER_TEXT              , TT_SHOW_SS_CUE_BANNER_TEXT   );
    ADD_TOOLTIP(IDC_EDIT_SIMILAR_PROCESS_GROUPS      , TT_SIMILAR_PROCESS_GROUPS    );
    ADD_TOOLTIP(IDC_EDIT_FUZZY_MATCH_PERCENT         , TT_FUZZY_MATCH_PERCENT       );
    ADD_TOOLTIP(IDC_STATIC_FUZZY_MATCH_PERCENT       , TT_FUZZY_MATCH_PERCENT       );
    ADD_TOOLTIP(IDC_EDIT_WINDOW_TRANSPARENCY         , TT_WINDOW_TRANSPARENCY       );
    ADD_TOOLTIP(IDC_EDIT_WINDOW_WIDTH_PERCENTAGE     , TT_WINDOW_WIDTH_PERCENT      );
    ADD_TOOLTIP(IDC_EDIT_WINDOW_HEIGHT_PERCENTAGE    , TT_WINDOW_HEIGHT_PERCENT     );
    ADD_TOOLTIP(IDC_CHECK_PROMPT_TERMINATE_ALL       , TT_PROMPT_TERMINATE_ALL      );
    ADD_TOOLTIP(IDC_CHECK_SHOW_SEARCH_STRING         , TT_SHOW_SEARCH_STRING        );
    ADD_TOOLTIP(IDC_CHECK_SHOW_COL_HEADER            , TT_SHOW_COLUMN_HEADER        );
    ADD_TOOLTIP(IDC_CHECK_SHOW_COL_PROCESSNAME       , TT_SHOW_COLUMN_PROCESS_NAME  );
    ADD_TOOLTIP(IDC_CHECK_MH_SHOW_PROCESSINFO_TOOLTIP, TT_SHOW_PROCESSINFO_TOOLTIP  );
    ADD_TOOLTIP(IDC_CHECK_MH_SHOW_HIGHLIGHT_RECT     , TT_SHOW_MOUSEOVER_ITEM       );
    ADD_TOOLTIP(IDC_CHECK_MH_SHOW_DELETE_BUTTON      , TT_SHOW_DELETE_BUTTON        );
    ADD_TOOLTIP(IDC_CHECK_FOR_UPDATES                , TT_CHECK_FOR_UPDATES         );
    ADD_TOOLTIP(IDC_CHECK_PROCESS_EXCLUSIONS         , TT_CHECK_PROCESS_EXCLUSIONS  );
    ADD_TOOLTIP(IDC_EDIT_PROCESS_EXCLUSIONS          , TT_EDIT_PROCESS_EXCLUSIONS   );
    ADD_TOOLTIP(IDC_BUTTON_APPLY                     , TT_APPLY_SETTINGS            );
    ADD_TOOLTIP(IDOK                                 , TT_OK_SETTINGS               );
    ADD_TOOLTIP(IDCANCEL                             , TT_CANCEL_SETTINGS           );
    ADD_TOOLTIP(IDC_BUTTON_RESET                     , TT_RESET_SETTINGS            );
    ADD_TOOLTIP(IDC_BUTTON_RELOAD                    , TT_RELOAD_SETTINGS           );
}

const wchar_t* PROP_EDIT_FONT = L"AltTab_SettingsEditFont";
const wchar_t* PROP_BOLD_FONT = L"AltTab_SettingsBoldFont";

/*!
 * \brief Lazily create a dialog-lifetime font stored as a window property.
 */
HFONT GetDialogFont(HWND hDlg, const wchar_t* propName, const LOGFONTW& lf) {
    HFONT hFont = (HFONT)GetPropW(hDlg, propName);
    if (!hFont) {
        hFont = CreateFontIndirectW(&lf);
        SetPropW(hDlg, propName, hFont);
    }
    return hFont;
}

void DestroyDialogFonts(HWND hDlg) {
    HFONT hEditFont = (HFONT)GetPropW(hDlg, PROP_EDIT_FONT);
    HFONT hBoldFont = (HFONT)GetPropW(hDlg, PROP_BOLD_FONT);

    if (hEditFont) {
        RemovePropW(hDlg, PROP_EDIT_FONT);
        DeleteObject(hEditFont);
    }
    if (hBoldFont) {
        RemovePropW(hDlg, PROP_BOLD_FONT);
        DeleteObject(hBoldFont);
    }
}

/*!
 * \brief Get the text of a dialog item.
 */
std::wstring GetDlgItemTextEx(HWND hDlg, int nIDDlgItem) {
    const int textLength = GetWindowTextLength(GetDlgItem(hDlg, nIDDlgItem));
    std::wstring buffer(textLength + 1, L'\0');
    GetDlgItemTextW(hDlg, nIDDlgItem, &buffer[0], textLength + 1);
    buffer.resize(textLength);
    return buffer;
}

/*!
 * \brief Log the current AltTab settings (debug builds).
 */
void ATLogSettings(const AltTabSettings& settings) {
#ifdef _AT_LOGGER
    AT_LOG_TRACE;
    AT_LOG_DEBUG("=== AltTab Settings Begin ===");
    AT_LOG_DEBUG("[Hotkeys]");
    AT_LOG_DEBUG("  HKAltTabEnabled           : [%s]", BOOL_TO_CSTR(settings.HKAltTabEnabled));
    AT_LOG_DEBUG("  HKAltBacktickEnabled      : [%s]", BOOL_TO_CSTR(settings.HKAltBacktickEnabled));
    AT_LOG_DEBUG("  HKAltCtrlTabEnabled       : [%s]", BOOL_TO_CSTR(settings.HKAltCtrlTabEnabled));
    AT_LOG_DEBUG("[SearchString]");
    AT_LOG_DEBUG("  SSCueBannerText           : [%s]", WStrToUTF8(settings.SSCueBannerText).c_str());
    AT_LOG_DEBUG("  SSFontName                : [%s]", WStrToUTF8(settings.SSFontName).c_str());
    AT_LOG_DEBUG("  SSFontSize                : [%d]", settings.SSFontSize);
    AT_LOG_DEBUG("  SSFontStyle               : [%s]", WStrToUTF8(settings.SSFontStyle).c_str());
    AT_LOG_DEBUG("  SSFontColor               : [%s]", WStrToUTF8(ColorRefToRGBString(settings.SSFontColor)).c_str());
    AT_LOG_DEBUG("  SSBackgroundColor         : [%s]", WStrToUTF8(ColorRefToRGBString(settings.SSBackgroundColor)).c_str());
    AT_LOG_DEBUG("[ListView]");
    AT_LOG_DEBUG("  LVFontName                : [%s]", WStrToUTF8(settings.LVFontName).c_str());
    AT_LOG_DEBUG("  LVFontSize                : [%d]", settings.LVFontSize);
    AT_LOG_DEBUG("  LVFontStyle               : [%s]", WStrToUTF8(settings.LVFontStyle).c_str());
    AT_LOG_DEBUG("  LVFontColor               : [%s]", WStrToUTF8(ColorRefToRGBString(settings.LVFontColor)).c_str());
    AT_LOG_DEBUG("  LVBackgroundColor         : [%s]", WStrToUTF8(ColorRefToRGBString(settings.LVBackgroundColor)).c_str());
    AT_LOG_DEBUG(
        "  LVHighlightTextColor      : [%s]",
        WStrToUTF8(ColorRefToRGBString(settings.LVHighlightTextColor)).c_str());
    AT_LOG_DEBUG(
        "  LVHighlightBackgroundColor: [%s]",
        WStrToUTF8(ColorRefToRGBString(settings.LVHighlightBackgroundColor)).c_str());
    AT_LOG_DEBUG("[General]");
    AT_LOG_DEBUG("  FuzzyMatchPercent         : [%d]", settings.FuzzyMatchPercent);
    AT_LOG_DEBUG("  Transparency              : [%d]", settings.Transparency);
    AT_LOG_DEBUG("  WidthPercentage           : [%d]", settings.WidthPercentage);
    AT_LOG_DEBUG("  HeightPercentage          : [%d]", settings.HeightPercentage);
    AT_LOG_DEBUG("  CheckForUpdatesOpt        : [%s]", WStrToUTF8(settings.CheckForUpdatesOpt).c_str());
    AT_LOG_DEBUG("  PromptTerminateAll        : [%s]", BOOL_TO_CSTR(settings.PromptTerminateAll));
    AT_LOG_DEBUG("  ShowSearchString          : [%s]", BOOL_TO_CSTR(settings.ShowSearchString));
    AT_LOG_DEBUG("  ShowColHeader             : [%s]", BOOL_TO_CSTR(settings.ShowColHeader));
    AT_LOG_DEBUG("  ShowColProcessName        : [%s]", BOOL_TO_CSTR(settings.ShowColProcessName));
    AT_LOG_DEBUG("[MouseHover]");
    AT_LOG_DEBUG("  ShowProcessInfoTooltip    : [%s]", BOOL_TO_CSTR(settings.ShowProcessInfoTooltip));
    AT_LOG_DEBUG("  ShowHighlightRect         : [%s]", BOOL_TO_CSTR(settings.ShowHighlightRect));
    AT_LOG_DEBUG("  ShowDeleteButton          : [%s]", BOOL_TO_CSTR(settings.ShowDeleteButton));
    AT_LOG_DEBUG("[Backtick]");
    AT_LOG_DEBUG("  SimilarProcessGroups      : [%s]", WStrToUTF8(settings.SimilarProcessGroups).c_str());
    AT_LOG_DEBUG("[ProcessExclusions]");
    AT_LOG_DEBUG("  ProcessExclusionsEnabled  : [%s]", BOOL_TO_CSTR(settings.ProcessExclusionsEnabled));
    AT_LOG_DEBUG("  ProcessExclusions         : [%s]", WStrToUTF8(settings.ProcessExclusions).c_str());
    AT_LOG_DEBUG("=== AltTab Settings End ===");
#else
    (void)settings;
#endif // _AT_LOGGER
}

/*!
 * \brief Read the settings controls' current values into settings.
 */
void ATReadSettingsFromUI(HWND hDlg, AltTabSettings& settings) {
    settings.SSCueBannerText          = GetDlgItemTextEx(hDlg, IDC_EDIT_SS_BANNER_TEXT);
    settings.SimilarProcessGroups     = GetDlgItemTextEx(hDlg, IDC_EDIT_SIMILAR_PROCESS_GROUPS);
    settings.FuzzyMatchPercent        = GetDlgItemInt(hDlg, IDC_EDIT_FUZZY_MATCH_PERCENT, nullptr, FALSE);
    settings.Transparency             = GetDlgItemInt(hDlg, IDC_EDIT_WINDOW_TRANSPARENCY, nullptr, FALSE);
    settings.WidthPercentage          = GetDlgItemInt(hDlg, IDC_EDIT_WINDOW_WIDTH_PERCENTAGE, nullptr, FALSE);
    settings.HeightPercentage         = GetDlgItemInt(hDlg, IDC_EDIT_WINDOW_HEIGHT_PERCENTAGE, nullptr, FALSE);
    settings.PromptTerminateAll       = IsDlgButtonChecked(hDlg, IDC_CHECK_PROMPT_TERMINATE_ALL) == BST_CHECKED;
    settings.ShowSearchString         = IsDlgButtonChecked(hDlg, IDC_CHECK_SHOW_SEARCH_STRING) == BST_CHECKED;
    settings.ShowColHeader            = IsDlgButtonChecked(hDlg, IDC_CHECK_SHOW_COL_HEADER) == BST_CHECKED;
    settings.ShowColProcessName       = IsDlgButtonChecked(hDlg, IDC_CHECK_SHOW_COL_PROCESSNAME) == BST_CHECKED;
    settings.HKAltTabEnabled          = IsDlgButtonChecked(hDlg, IDC_CHECK_ALT_TAB) == BST_CHECKED;
    settings.HKAltBacktickEnabled     = IsDlgButtonChecked(hDlg, IDC_CHECK_ALT_BACKTICK) == BST_CHECKED;
    settings.HKAltCtrlTabEnabled      = IsDlgButtonChecked(hDlg, IDC_CHECK_ALT_CTRL_TAB) == BST_CHECKED;
    settings.ShowProcessInfoTooltip   = IsDlgButtonChecked(hDlg, IDC_CHECK_MH_SHOW_PROCESSINFO_TOOLTIP) == BST_CHECKED;
    settings.ShowHighlightRect        = IsDlgButtonChecked(hDlg, IDC_CHECK_MH_SHOW_HIGHLIGHT_RECT) == BST_CHECKED;
    settings.ShowDeleteButton         = IsDlgButtonChecked(hDlg, IDC_CHECK_MH_SHOW_DELETE_BUTTON) == BST_CHECKED;
    settings.ProcessExclusionsEnabled = IsDlgButtonChecked(hDlg, IDC_CHECK_PROCESS_EXCLUSIONS) == BST_CHECKED;
    settings.ProcessExclusions        = GetDlgItemTextEx(hDlg, IDC_EDIT_PROCESS_EXCLUSIONS);

    const int selectedIndex = ComboBox_GetCurSel(GetDlgItem(hDlg, IDC_CHECK_FOR_UPDATES));
    const int index = max(selectedIndex, 0);
    if (index < (int)AltTabSettings::CheckForUpdatesOptions.size()) {
        settings.CheckForUpdatesOpt = AltTabSettings::CheckForUpdatesOptions[index];
    }
}

/*!
 * \brief Check whether the dialog's current values differ from g_Settings.
 */
bool AreSettingsModified(HWND hDlg) {
    AltTabSettings settings;
    ATReadSettingsFromUI(hDlg, settings);

    return settings.FuzzyMatchPercent        != g_Settings.FuzzyMatchPercent
        || settings.Transparency             != g_Settings.Transparency
        || settings.WidthPercentage          != g_Settings.WidthPercentage
        || settings.HeightPercentage         != g_Settings.HeightPercentage
        || settings.CheckForUpdatesOpt       != g_Settings.CheckForUpdatesOpt
        || settings.PromptTerminateAll       != g_Settings.PromptTerminateAll
        || settings.ShowSearchString         != g_Settings.ShowSearchString
        || settings.ShowColHeader            != g_Settings.ShowColHeader
        || settings.ShowColProcessName       != g_Settings.ShowColProcessName
        || settings.HKAltTabEnabled          != g_Settings.HKAltTabEnabled
        || settings.HKAltBacktickEnabled     != g_Settings.HKAltBacktickEnabled
        || settings.HKAltCtrlTabEnabled      != g_Settings.HKAltCtrlTabEnabled
        || settings.ProcessExclusionsEnabled != g_Settings.ProcessExclusionsEnabled
        || settings.ProcessExclusions        != g_Settings.ProcessExclusions
        || settings.SimilarProcessGroups     != g_Settings.SimilarProcessGroups
        || settings.SSCueBannerText          != g_Settings.SSCueBannerText
        || settings.ShowProcessInfoTooltip   != g_Settings.ShowProcessInfoTooltip
        || settings.ShowHighlightRect        != g_Settings.ShowHighlightRect
        || settings.ShowDeleteButton         != g_Settings.ShowDeleteButton;
}

/*!
 * \brief Validate and apply the dialog settings to g_Settings and the INI file.
 */
void ATApplySettings(HWND hDlg) {
    AT_LOG_TRACE;

    // Copy the existing settings then merge only the values shown in the UI,
    // so hidden settings (fonts, colors, hotkeys) are preserved.
    AltTabSettings settings(g_Settings);
    ATReadSettingsFromUI(hDlg, settings);

#ifdef _DEBUG
    ATLogSettings(settings);
#endif // _DEBUG

    bool isValid = false;
    const auto errorInfo = settings.IsValid(isValid);
    if (!isValid) {
        MessageBox(hDlg, errorInfo.second.c_str(), errorInfo.first.c_str(), MB_ICONERROR | MB_OK);
        return;
    }

    g_Settings = settings;
    ATSaveSettings();

    // Reload so the ProcessGroupsList / ProcessExclusionList are rebuilt.
    ATLoadSettings();

    EnableWindow(GetDlgItem(hDlg, IDC_BUTTON_APPLY), false);
}

/*!
 * \brief Initialize the dialog controls from the given settings.
 */
void ATSettingsInitDialog(HWND hDlg, const AltTabSettings& settings) {
    SetDlgItemText(hDlg, IDC_EDIT_SETTINGS_FILEPATH, ATSettingsFilePath().c_str());
    SetDlgItemText(hDlg, IDC_EDIT_SS_BANNER_TEXT, settings.SSCueBannerText.c_str());
    SetDlgItemText(hDlg, IDC_EDIT_SIMILAR_PROCESS_GROUPS, settings.SimilarProcessGroups.c_str());

    // Monospace font for the process group / exclusion editors.
    LOGFONTW lfMonospace = {};
    lfMonospace.lfHeight       = -MulDiv(14, 96, 72);
    lfMonospace.lfWeight       = FW_NORMAL;
    lfMonospace.lfCharSet      = DEFAULT_CHARSET;
    lfMonospace.lfPitchAndFamily = DEFAULT_PITCH | FF_SWISS;
    wcscpy_s(lfMonospace.lfFaceName, LF_FACESIZE, L"Lucida Console");
    HFONT hFont = GetDialogFont(hDlg, PROP_EDIT_FONT, lfMonospace);

    SendMessageW(GetDlgItem(hDlg, IDC_EDIT_SIMILAR_PROCESS_GROUPS), WM_SETFONT, (WPARAM)hFont, TRUE);
    SendMessageW(GetDlgItem(hDlg, IDC_EDIT_PROCESS_EXCLUSIONS), WM_SETFONT, (WPARAM)hFont, TRUE);

    CheckDlgButton(hDlg, IDC_CHECK_ALT_TAB, settings.HKAltTabEnabled ? BST_CHECKED : BST_UNCHECKED);
    CheckDlgButton(hDlg, IDC_CHECK_ALT_BACKTICK, settings.HKAltBacktickEnabled ? BST_CHECKED : BST_UNCHECKED);
    CheckDlgButton(hDlg, IDC_CHECK_ALT_CTRL_TAB, settings.HKAltCtrlTabEnabled ? BST_CHECKED : BST_UNCHECKED);
    CheckDlgButton(hDlg, IDC_CHECK_PROMPT_TERMINATE_ALL, settings.PromptTerminateAll ? BST_CHECKED : BST_UNCHECKED);
    CheckDlgButton(hDlg, IDC_CHECK_SHOW_SEARCH_STRING, settings.ShowSearchString ? BST_CHECKED : BST_UNCHECKED);
    CheckDlgButton(hDlg, IDC_CHECK_SHOW_COL_HEADER, settings.ShowColHeader ? BST_CHECKED : BST_UNCHECKED);
    CheckDlgButton(hDlg, IDC_CHECK_SHOW_COL_PROCESSNAME, settings.ShowColProcessName ? BST_CHECKED : BST_UNCHECKED);
    CheckDlgButton(hDlg, IDC_CHECK_PROCESS_EXCLUSIONS, settings.ProcessExclusionsEnabled ? BST_CHECKED : BST_UNCHECKED);
    CheckDlgButton(hDlg, IDC_CHECK_MH_SHOW_PROCESSINFO_TOOLTIP,
                   settings.ShowProcessInfoTooltip ? BST_CHECKED : BST_UNCHECKED);
    CheckDlgButton(hDlg, IDC_CHECK_MH_SHOW_HIGHLIGHT_RECT, settings.ShowHighlightRect ? BST_CHECKED : BST_UNCHECKED);
    CheckDlgButton(hDlg, IDC_CHECK_MH_SHOW_DELETE_BUTTON, settings.ShowDeleteButton ? BST_CHECKED : BST_UNCHECKED);

    EnableWindow(GetDlgItem(hDlg, IDC_EDIT_PROCESS_EXCLUSIONS), settings.ProcessExclusionsEnabled);

    SetDlgItemInt(hDlg, IDC_EDIT_FUZZY_MATCH_PERCENT, settings.FuzzyMatchPercent, FALSE);
    SendDlgItemMessage(hDlg, IDC_SPIN_FUZZY_MATCH_PERCENT, UDM_SETRANGE, 0, MAKELPARAM(100, 0));
    SendDlgItemMessage(hDlg, IDC_SPIN_FUZZY_MATCH_PERCENT, UDM_SETPOS, 0, MAKELPARAM(settings.FuzzyMatchPercent, 0));

    SetDlgItemInt(hDlg, IDC_EDIT_WINDOW_TRANSPARENCY, settings.Transparency, FALSE);
    SendDlgItemMessage(hDlg, IDC_SPIN_WINDOW_TRANSPARENCY, UDM_SETRANGE, 0, MAKELPARAM(255, 0));
    SendDlgItemMessage(hDlg, IDC_SPIN_WINDOW_TRANSPARENCY, UDM_SETPOS, 0, MAKELPARAM(settings.Transparency, 0));

    SetDlgItemInt(hDlg, IDC_EDIT_WINDOW_WIDTH_PERCENTAGE, settings.WidthPercentage, FALSE);
    SendDlgItemMessage(hDlg, IDC_SPIN_WINDOW_WIDTH_PERCENTAGE, UDM_SETRANGE, 0, MAKELPARAM(90, 10));
    SendDlgItemMessage(hDlg, IDC_SPIN_WINDOW_WIDTH_PERCENTAGE, UDM_SETPOS, 0, MAKELPARAM(settings.WidthPercentage, 0));

    SetDlgItemInt(hDlg, IDC_EDIT_WINDOW_HEIGHT_PERCENTAGE, settings.HeightPercentage, FALSE);
    SendDlgItemMessage(hDlg, IDC_SPIN_WINDOW_HEIGHT_PERCENTAGE, UDM_SETRANGE, 0, MAKELPARAM(90, 10));
    SendDlgItemMessage(hDlg, IDC_SPIN_WINDOW_HEIGHT_PERCENTAGE, UDM_SETPOS, 0, MAKELPARAM(settings.HeightPercentage, 0));

    SetDlgItemText(hDlg, IDC_EDIT_PROCESS_EXCLUSIONS, settings.ProcessExclusions.c_str());

    // CheckForUpdates combo (reset content on re-init to avoid duplicates).
    HWND hComboBox = GetDlgItem(hDlg, IDC_CHECK_FOR_UPDATES);
    ComboBox_ResetContent(hComboBox);
    for (const auto& opt : AltTabSettings::CheckForUpdatesOptions) {
        ComboBox_AddString(hComboBox, opt.c_str());
    }
    ComboBox_SetCurSel(hComboBox, settings.GetCheckForUpdatesIndex());

    // Center the dialog on the screen.
    const int screenWidth  = GetSystemMetrics(SM_CXSCREEN);
    const int screenHeight = GetSystemMetrics(SM_CYSCREEN);
    RECT dlgRect;
    GetWindowRect(hDlg, &dlgRect);

    const int dlgWidth  = dlgRect.right - dlgRect.left;
    const int dlgHeight = dlgRect.bottom - dlgRect.top;

    const int posX = (screenWidth - dlgWidth) / 2;
    const int posY = (screenHeight - dlgHeight) / 2;

    SetWindowPos(hDlg, HWND_TOP, posX, posY, 0, 0, SWP_NOSIZE);

    // Show the dialog in the taskbar.
    SetWindowLong(hDlg, GWL_EXSTYLE, GetWindowLong(hDlg, GWL_EXSTYLE) | WS_EX_APPWINDOW);

    EnableWindow(GetDlgItem(hDlg, IDC_BUTTON_APPLY), AreSettingsModified(hDlg));

    // Bold group-box captions.
    HFONT hDlgFont = (HFONT)SendMessageW(hDlg, WM_GETFONT, 0, 0);
    LOGFONTW lfBold = {};
    if (hDlgFont) {
        GetObjectW(hDlgFont, sizeof(LOGFONTW), &lfBold);
    } else {
        lfBold.lfHeight = -MulDiv(8, 96, 72);
        lfBold.lfCharSet = DEFAULT_CHARSET;
    }
    lfBold.lfWeight = FW_BOLD;
    HFONT hBoldFont = GetDialogFont(hDlg, PROP_BOLD_FONT, lfBold);

    SendMessageW(GetDlgItem(hDlg, IDC_GROUPBOX_STORAGE), WM_SETFONT, (WPARAM)hBoldFont, TRUE);
    SendMessageW(GetDlgItem(hDlg, IDC_GROUPBOX_HOTKEYS), WM_SETFONT, (WPARAM)hBoldFont, TRUE);
    SendMessageW(GetDlgItem(hDlg, IDC_GROUPBOX_GENERAL), WM_SETFONT, (WPARAM)hBoldFont, TRUE);
    SendMessageW(GetDlgItem(hDlg, IDC_GROUPBOX_MOUSEHOVER), WM_SETFONT, (WPARAM)hBoldFont, TRUE);
    SendMessageW(GetDlgItem(hDlg, IDC_GROUPBOX_BACKTICK), WM_SETFONT, (WPARAM)hBoldFont, TRUE);
    SendMessageW(GetDlgItem(hDlg, IDC_GROUPBOX_PROCESS_EXCLUSIONS), WM_SETFONT, (WPARAM)hBoldFont, TRUE);
}

} // namespace

INT_PTR CALLBACK ATSettingsDlgProc(HWND hDlg, UINT message, WPARAM wParam, LPARAM lParam) {
    UNREFERENCED_PARAMETER(lParam);
    switch (message) {
    case WM_INITDIALOG: {
        g_hSettingsWnd = hDlg;

        HICON hIcon = LoadIcon(g_hInstance, MAKEINTRESOURCE(IDI_ALTTAB));
        SendMessageW(hDlg, WM_SETICON, ICON_SMALL, (LPARAM)hIcon);
        SendMessageW(hDlg, WM_SETICON, ICON_BIG, (LPARAM)hIcon);

        ATSettingsInitDialog(hDlg, g_Settings);
        AddTooltips(hDlg);
        return (INT_PTR)TRUE;
    }

    case WM_CTLCOLOREDIT: {
        const HDC hdcEdit = (HDC)wParam;
        const HWND hEdit = (HWND)lParam;
        const int id = GetDlgCtrlID(hEdit);
        if (id == IDC_EDIT_SIMILAR_PROCESS_GROUPS || id == IDC_EDIT_PROCESS_EXCLUSIONS) {
            SetTextColor(hdcEdit, RGB(0, 0, 255)); // blue text
        }
        return (INT_PTR)GetSysColorBrush(COLOR_WINDOW);
    }

    case WM_COMMAND: {
        const bool settingsModified = AreSettingsModified(hDlg);
        EnableWindow(GetDlgItem(hDlg, IDC_BUTTON_APPLY), settingsModified);

        const int commandId = LOWORD(wParam);

        if (commandId == IDC_BUTTON_APPLY) {
            AT_LOG_INFO("IDC_BUTTON_APPLY: Apply Settings");
            if (settingsModified) {
                ATApplySettings(hDlg);
            }
            return (INT_PTR)TRUE;
        }

        if (commandId == IDOK) {
            if (settingsModified) {
                ATApplySettings(hDlg);
            }
            EndDialog(hDlg, LOWORD(wParam));
            return (INT_PTR)TRUE;
        }

        if (commandId == IDCANCEL) {
            AT_LOG_INFO("IDCANCEL: Cancel Pressed!");
            EndDialog(hDlg, LOWORD(wParam));
            return (INT_PTR)TRUE;
        }

        if (commandId == IDC_BUTTON_RESET) {
            AT_LOG_INFO("IDC_BUTTON_RESET Pressed!");
            const int result = MessageBoxW(
                hDlg,
                L"Are you sure you want to reset settings to defaults?",
                AT_PRODUCT_NAMEW L": Reset Settings",
                MB_YESNO | MB_ICONQUESTION | MB_DEFBUTTON2);
            if (result == IDYES) {
                g_Settings.Reset();
                g_Settings.Save();
                ATSettingsInitDialog(hDlg, g_Settings);
            }
            return (INT_PTR)TRUE;
        }

        if (commandId == IDC_BUTTON_RELOAD) {
            AT_LOG_INFO("IDC_BUTTON_RELOAD Pressed!");
            const int result = MessageBoxW(
                hDlg,
                L"Are you sure you want to reload settings from AltTabSettings.ini?",
                AT_PRODUCT_NAMEW L": Reload Settings",
                MB_YESNO | MB_ICONQUESTION | MB_DEFBUTTON2);
            if (result == IDYES) {
                g_Settings.Load();
#ifdef _DEBUG
                ATLogSettings(g_Settings);
#endif // _DEBUG
                ATSettingsInitDialog(hDlg, g_Settings);
            }
            return (INT_PTR)TRUE;
        }

        if (commandId == IDC_CHECK_PROCESS_EXCLUSIONS) {
            AT_LOG_INFO("IDC_CHECK_PROCESS_EXCLUSIONS Clicked!");
            const bool isChecked = IsDlgButtonChecked(hDlg, IDC_CHECK_PROCESS_EXCLUSIONS) == BST_CHECKED;
            EnableWindow(GetDlgItem(hDlg, IDC_EDIT_PROCESS_EXCLUSIONS), isChecked);
            return (INT_PTR)TRUE;
        }
    }
    break;

    case WM_DESTROY: {
        DestroyDialogFonts(hDlg);

        DestroyIcon((HICON)SendMessageW(hDlg, WM_GETICON, ICON_SMALL, 0));
        DestroyIcon((HICON)SendMessageW(hDlg, WM_GETICON, ICON_BIG, 0));

        g_hSettingsWnd = nullptr;
    }
    break;
    }

    return (INT_PTR)FALSE;
}
#include "PreCompile.h"
#include "AltTabSettings.h"
#include "Resource.h"
#include <windowsx.h>
#include <CommCtrl.h>
#include "Utils.h"
#include "Logger.h"
#include <ShlObj_core.h>
#include "version.h"
#include <iostream>
#include <fstream>
#include <filesystem>
#include "GlobalData.h"
#include "AltTabWindow.h"
#include "Tooltips.h"
#include <regex>

#define WM_SETTEXTCOLOR (WM_USER + 1)
#define BOOL_TO_CSTR(b) ((b) ? "true" : "false")


AltTabSettings    g_Settings;
AltTabWindowData  g_AltBacktickWndInfo;
HWND              g_hToolTip           = nullptr;


#define ADD_TOOLTIP(id, text)                                                    \
    toolInfo.hwnd = GetDlgItem(hDlg, id);                                        \
    toolInfo.lpszText = (LPWSTR)text;                                            \
    GetClientRect(toolInfo.hwnd, &toolInfo.rect);                                \
    SendMessageW(g_hToolTip, TTM_ADDTOOL, 0, reinterpret_cast<LPARAM>(&toolInfo));


void ATSettingsInitDialog(HWND hDlg, const AltTabSettings& settings);
void ATReadSettingsFromUI(HWND hDlg, AltTabSettings& settings);
void AddTooltips         (HWND hDlg);
void ATLogSettings       (const AltTabSettings& settings);
int64_t HexToDecimal(const std::wstring& hexStr);

namespace {
    // Sections
    const wchar_t* SEARCH_STRING             = L"SearchString"            ;
    const wchar_t* LIST_VIEW                 = L"ListView"                ;
    const wchar_t* GENERAL                   = L"General"                 ;
    const wchar_t* HOTKEYS                   = L"Hotkeys"                 ;
    const wchar_t* MOUSE_HOVER               = L"MouseHover"              ;
    const wchar_t* BACKTICK                  = L"Backtick"                ;
    const wchar_t* PROCESS_EXCLUSIONS        = L"ProcessExclusions"       ;

    const wchar_t* CUE_BANNER_TEXT           = L"CueBannerText"           ;
    const wchar_t* FONT_NAME                 = L"FontName"                ;
    const wchar_t* FONT_SIZE                 = L"FontSize"                ;
    const wchar_t* FONT_STYLE                = L"FontStyle"               ;
    const wchar_t* FONT_COLOR                = L"FontColor"               ;
    const wchar_t* BACKGROUND_COLOR          = L"BackgroundColor"         ;
    const wchar_t* HIGHLIGHT_TEXT_COLOR      = L"HighlightTextColor"      ;
    const wchar_t* HIGHLIGHT_BG_COLOR        = L"HighlightBackgroundColor";

    const wchar_t* PROMPT_TERMINATE_ALL      = L"PromptTerminateAll"      ;
    const wchar_t* FUZZY_MATCH_PERCENT       = L"FuzzyMatchPercent"       ;
    const wchar_t* WINDOW_TRANSPARENCY       = L"WindowTransparency"      ;
    const wchar_t* WINDOW_WIDTH_PERCENTAGE   = L"WindowWidthPercentage"   ;
    const wchar_t* WINDOW_HEIGHT_PERCENTAGE  = L"WindowHeightPercentage"  ;
    const wchar_t* SHOW_SEARCH_STRING        = L"ShowSearchString"        ;
    const wchar_t* SHOW_COL_HEADER           = L"ShowColHeader"           ;
    const wchar_t* SHOW_COL_PROCESSNAME      = L"ShowColProcessName"      ;
    const wchar_t* SHOW_PROCESS_INFO_TOOLTIP = L"ShowProcessInfoTooltip"  ;
    const wchar_t* SHOW_HIGHLIGHT_RECT       = L"ShowHighlightRect"       ;
    const wchar_t* SHOW_DELETE_BUTTON        = L"ShowDeleteButton"        ;

    const wchar_t* CHECK_FOR_UPDATES         = L"CheckForUpdates"         ;
    const wchar_t* SYSTEM_TRAY_ICON_ENABLED  = L"SystemTrayIconEnabled"   ;
    const wchar_t* ALTTAB_ENABLED            = L"AltTabEnabled"           ;
    const wchar_t* ALTBACKTICK_ENABLED       = L"AltBacktickEnabled"      ;
    const wchar_t* ALTCTRLTAB_ENABLED        = L"AltCtrlTabEnabled"       ;
    const wchar_t* BACKTICK_KEY              = L"BacktickKey";
    const wchar_t* SIMILAR_PROCESS_GROUPS    = L"SimilarProcessGroups"    ;
    const wchar_t* ENABLED                   = L"Enabled"                 ;
    const wchar_t* PROCESS_LIST              = L"ProcessList"             ;

    // Convert COLORREF to 0xRRGGBB format
    std::wstring ColorRefToRGBString(COLORREF colorRef) {
        wchar_t buffer[9]; // Format: 0xRRGGBB\0
        swprintf(buffer, 9, L"0x%02X%02X%02X", GetRValue(colorRef), GetGValue(colorRef), GetBValue(colorRef));
        return buffer;
    }

    // Construct COLORREF from RGB integer value
    COLORREF RGBIntToColorRef(int hexValue) {
        // Extract individual components
        int red   = (hexValue >> 16) & 0xFF;
        int green = (hexValue >>  8) & 0xFF;
        int blue  = (hexValue      ) & 0xFF;

        // Combine components into COLORREF format
        return RGB(red, green, blue);
    }
}

/*!
 * \brief Constructor
 */
AltTabSettings::AltTabSettings() {
    Reset();
}

/*!
 * \brief Reset settings to default values.
 */
void AltTabSettings::Reset() {
    HKAltTabEnabled            = DEFAULT_ALT_TAB_ENABLED            ;
    HKAltBacktickEnabled       = DEFAULT_ALT_BACKTICK_ENABLED       ;
    HKAltCtrlTabEnabled        = DEFAULT_ALT_CTRL_TAB_ENABLED       ;
    HKBacktickKey              = HexToDecimal(DEFAULT_BACKTICK_KEY);
    SSCueBannerText            = DEFAULT_SS_CUE_BANNER_TEXT         ;
    SSFontName                 = DEFAULT_SS_FONT_NAME               ;
    SSFontSize                 = DEFAULT_SS_FONT_SIZE               ;
    SSFontStyle                = DEFAULT_SS_FONT_STYLE              ;
    SSFontColor                = DEFAULT_SS_FONT_COLOR              ;
    SSBackgroundColor          = DEFAULT_SS_BG_COLOR                ;
    LVFontName                 = DEFAULT_LV_FONT_NAME               ;
    LVFontSize                 = DEFAULT_LV_FONT_SIZE               ;
    LVFontStyle                = DEFAULT_LV_FONT_STYLE              ;
    LVFontColor                = DEFAULT_LV_FONT_COLOR              ;
    LVBackgroundColor          = DEFAULT_LV_BG_COLOR                ;
    LVHighlightTextColor       = DEFAULT_LV_HIGHLIGHT_TEXT_COLOR    ;
    LVHighlightBackgroundColor = DEFAULT_LV_HIGHLIGHT_BG_COLOR      ;
    WidthPercentage            = DEFAULT_WIDTH                      ;
    HeightPercentage           = DEFAULT_HEIGHT                     ;
    FuzzyMatchPercent          = DEFAULT_FUZZYMATCHPERCENT          ;
    Transparency               = DEFAULT_TRANSPARENCY               ;
    SimilarProcessGroups       = DEFAULT_SIMILARPROCESSGROUPS       ;
    CheckForUpdatesOpt         = DEFAULT_CHECKFORUPDATES            ;
    PromptTerminateAll         = DEFAULT_PROMPTTERMINATEALL         ;
    DisableAltTab              = false                              ;
    ShowSearchString           = DEFAULT_SHOW_SEARCH_STRING         ;
    ShowColHeader              = DEFAULT_SHOW_COL_HEADER            ;
    ShowColProcessName         = DEFAULT_SHOW_COL_PROCESSNAME       ;
    ShowProcessInfoTooltip     = DEFAULT_MH_SHOW_PROCESSINFO_TOOLTIP;
    SystemTrayIconEnabled      = DEFAULT_SYSTEM_TRAY_ICON_ENABLED   ;
    ProcessExclusionsEnabled   = DEFAULT_PROCESS_EXCLUSIONS_ENABLED ;
    ProcessExclusions          = DEFAULT_PROCESS_EXCLUSIONS         ;

    // Clear the previous ProcessGroupsList
    g_Settings.ProcessGroupsList.clear();

    auto vs = Split(g_Settings.SimilarProcessGroups, L"|");
    for (auto& item : vs) {
        auto processes = Split(item, L"/");
        for (auto& processName : processes)
            processName = ToLower(processName);
        g_Settings.ProcessGroupsList.emplace_back(processes.begin(), processes.end());
    }

    // Process ProcessExclusions
    // Always split and convert to lower case, then it is easy while checking
    g_Settings.ProcessExclusionList.clear();
    g_Settings.ProcessExclusionList = Split(ToLower(g_Settings.ProcessExclusions), L"/");

    // Initialize additional settings
    g_AltBacktickWndInfo.hWnd   = nullptr;
    g_AltBacktickWndInfo.hOwner = nullptr;
}

/*!
 * \brief Load settings from AltTabSettings.ini file.
 */
void AltTabSettings::Load() {
    ATLoadSettings();
}

/*!
 * \brief Save current settings to AltTabSettings.ini file.
 */
void AltTabSettings::Save() {
    ATSaveSettings();
}

int AltTabSettings::GetCheckForUpdatesIndex() const {
    auto it = std::find(CheckForUpdatesOptions.begin(), CheckForUpdatesOptions.end(), this->CheckForUpdatesOpt);
    if (it == CheckForUpdatesOptions.end()) {
        return 0;
    }
    return (int)std::distance(CheckForUpdatesOptions.begin(), it);
}

/**
 * \brief Add tooltips to AltTab settings dialog controls.
 * 
 * \param hDlg
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

    TOOLINFO toolInfo = { 0 };
    toolInfo.cbSize   = sizeof(toolInfo);
    toolInfo.hwnd     = hDlg;
    toolInfo.uFlags   = TTF_SUBCLASS;

    // Enable multiple lines
    SendMessageW(g_hToolTip, TTM_SETMAXTIPWIDTH, 0, MAXINT);

    // TODO: Not working
    SendMessageW(g_hToolTip, TTM_SETTIPBKCOLOR, RGB(255, 255, 0), 0);

    ADD_TOOLTIP(IDC_CHECK_ALT_TAB                    , TT_HOTKEY_ALT_TAB            );
    ADD_TOOLTIP(IDC_CHECK_ALT_BACKTICK               , TT_HOTKEY_ALT_BACKTICK       );
    ADD_TOOLTIP(IDC_CHECK_ALT_CTRL_TAB               , TT_HOTKEY_ALT_CTRL_TAB       );
    ADD_TOOLTIP(IDC_EDIT_SETTINGS_FILEPATH           , TT_SETTINGS_FILEPATH         );
    ADD_TOOLTIP(IDC_EDIT_SS_BANNER_TEXT              , TT_SHOW_SS_CUE_BANNER_TEXT   );
    ADD_TOOLTIP(IDC_EDIT_SIMILAR_PROCESS_GROUPS      , TT_SIMILAR_PROCESS_GROUPS    );
    ADD_TOOLTIP(IDC_EDIT_FUZZY_MATCH_PERCENT         , TT_FUZZY_MATCH_PERCENT       );
    ADD_TOOLTIP(IDC_STATIC_FUZZY_MATCH_PERCENT       , TT_FUZZY_MATCH_PERCENT       ); // TODO: Not working for static controls
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

// ----------------------------------------------------------------------------
// Settings dialog procedure
// ----------------------------------------------------------------------------
INT_PTR CALLBACK ATSettingsDlgProc(HWND hDlg, UINT message, WPARAM wParam, LPARAM lParam) {
    UNREFERENCED_PARAMETER(lParam);
    switch (message)
    {
    case WM_INITDIALOG: {
        // Store the settings window handle in global variable
        g_hSettingsWnd     = hDlg;

        HICON hIcon        = LoadIcon(g_hInstance, MAKEINTRESOURCE(IDI_ALTTAB));

        SendMessageW(hDlg, WM_SETICON, ICON_SMALL, (LPARAM)hIcon);
        SendMessageW(hDlg, WM_SETICON, ICON_BIG, (LPARAM)hIcon);

        ATSettingsInitDialog(hDlg, g_Settings);

        AddTooltips(hDlg);
    }
    return (INT_PTR)TRUE;

    case WM_CTLCOLOREDIT: {
        HDC  hdcEdit = (HDC)wParam;
        HWND hEdit   = (HWND)lParam;
        int  id      = GetDlgCtrlID(hEdit);
        if (id == IDC_EDIT_SIMILAR_PROCESS_GROUPS || id == IDC_EDIT_PROCESS_EXCLUSIONS) {
            SetTextColor(hdcEdit, RGB(0, 0, 255));   // Blue text color
        }
        return (LRESULT)GetSysColorBrush(COLOR_WINDOW);
    }
    break;

    //case WM_CTLCOLORSTATIC:
    //{
    //    HDC  hdcStatic = (HDC)wParam;
    //    HWND hStatic   = (HWND)lParam;
    //    //AT_LOG_DEBUG("hStatic = %d", hStatic);
    //    if (hStatic == GetDlgItem(hDlg, IDC_STATIC_FUZZY_MATCH_PERCENT)) {
    //        AT_LOG_INFO("Control Found");
    //        SetTextColor(hdcStatic, RGB(0, 0, 0xFF));
    //        SetBkColor  (hdcStatic, TRANSPARENT);
    //        return (INT_PTR)GetStockObject(TRANSPARENT);
    //        //return (INT_PTR)CreateSolidBrush(RGB(0xFF, 0xFF, 0xFF));
    //    }
    //}
    //break;

    //case WM_CTLCOLORSTATIC: {
    //    // Handle WM_CTLCOLORSTATIC to customize the text color of the group box
    //    HDC  hdc    = (HDC)wParam;
    //    HWND hwnd   = (HWND)lParam;
    //    UINT ctrlID = GetDlgCtrlID(hwnd);

    //    // Check if the control is a group box (BS_GROUPBOX style)
    //    //if (GetWindowLong(hStatic, GWL_STYLE) & BS_GROUPBOX) {
    //    if (ctrlID == IDC_GROUPBOX_MOUSEHOVER) {
    //        AT_LOG_INFO("Control ID: %d is BS_GROUPBOX: Group Box Found", ctrlID);
    //        // Set the text color for the group box
    //        SetTextColor(hdc, RGB(0, 0, 255));
    //        SetBkMode (hdc, TRANSPARENT);

    //        // Return a handle to the brush for the background
    //        return (LRESULT)GetSysColorBrush(COLOR_BTNFACE);
    //    }
    //} break;

    case WM_DRAWITEM: {
        AT_LOG_INFO("WM_DRAWITEM: Draw Item");
    } break;

    case WM_COMMAND: {
        bool settingsModified = false;
        if (g_hSettingsWnd) {
            settingsModified = AreSettingsModified(hDlg);
            EnableWindow(GetDlgItem(hDlg, IDC_BUTTON_APPLY), settingsModified);
        }

        if (LOWORD(wParam) == IDC_BUTTON_APPLY) {
            AT_LOG_INFO("IDC_BUTTON_APPLY: Apply Settings");
            if (settingsModified) { ATApplySettings(hDlg); }
            return (INT_PTR)TRUE;
        }

        if (LOWORD(wParam) == IDOK)
        {
            if (settingsModified) { ATApplySettings(hDlg); }
            EndDialog(hDlg, LOWORD(wParam));
            return (INT_PTR)TRUE;
        }

        if (LOWORD(wParam) == IDCANCEL)
        {
            AT_LOG_INFO("IDCANEL: Cancel Pressed!");
            EndDialog(hDlg, LOWORD(wParam));
            return (INT_PTR)TRUE;
        }

        if (LOWORD(wParam) == IDC_BUTTON_RESET)
        {
            AT_LOG_INFO("IDC_BUTTON_RESET Pressed!");
            int result = MessageBoxW(
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

        if (LOWORD(wParam) == IDC_BUTTON_RELOAD)
        {
            AT_LOG_INFO("IDC_BUTTON_RELOAD Pressed!");
            int result = MessageBoxW(
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

        if (LOWORD(wParam) == IDC_CHECK_PROCESS_EXCLUSIONS) {
            AT_LOG_INFO("IDC_CHECK_PROCESS_EXCLUSIONS Clicked!");
            bool isChecked = IsDlgButtonChecked(hDlg, IDC_CHECK_PROCESS_EXCLUSIONS) == BST_CHECKED;
            EnableWindow(GetDlgItem(hDlg, IDC_EDIT_PROCESS_EXCLUSIONS), isChecked);
            return (INT_PTR)TRUE;
        }
    } break;

    //case WM_NOTIFY: {
    //    AT_LOG_INFO("WM_NOTIFY: Notify");
    //    LPNMHDR pnmh = (LPNMHDR)lParam;
    //    if (pnmh->code == TTN_NEEDTEXT) {
    //        LPNMTTDISPINFO pDispInfo = (LPNMTTDISPINFO)lParam;
    //        if (pDispInfo->hdr.idFrom == IDC_STATIC_FUZZY_MATCH_PERCENT) {
    //            // Set the tooltip text for the static control
    //            pDispInfo->lpszText = (LPWSTR)L"Tooltip for Static Control";
    //        }
    //    }
    //} break;

    case WM_DESTROY: {
        //  Clean up
        HWND  hEditBox = GetDlgItem(hDlg, IDC_EDIT_SIMILAR_PROCESS_GROUPS);
        HFONT hFont    = (HFONT)SendMessageW(hEditBox, WM_GETFONT, 0, 0);
        if (hFont) {
            DeleteObject(hFont);
        }
        DestroyIcon((HICON)SendMessageW(hDlg, WM_GETICON, ICON_SMALL, 0));
        DestroyIcon((HICON)SendMessageW(hDlg, WM_GETICON, ICON_BIG  , 0));

        g_hSettingsWnd = nullptr;
    }
    break;

    }
    return (INT_PTR)FALSE;
}

int GetProcessGroupIndex(const std::wstring& processName) {
    const std::wstring& processNameL = ToLower(processName);
    for (size_t ind = 0; ind < g_Settings.ProcessGroupsList.size(); ++ind) {
        if (g_Settings.ProcessGroupsList[ind].contains(processNameL)) {
            return (int)ind;
        }
    }
    return -1;
}

bool IsSimilarProcess(int index, const std::wstring& processName) {
    if (index == -1)
        return false;
    return g_Settings.ProcessGroupsList[index].contains(ToLower(processName));
}

bool IsSimilarProcess(const std::wstring& processNameA, const std::wstring& processNameB) {
    if (EqualsIgnoreCase(processNameA, processNameB))
        return true;

    int index = GetProcessGroupIndex(processNameA);
    if (index == -1)
        return false;

    return g_Settings.ProcessGroupsList[index].contains(ToLower(processNameB));
}

std::wstring ATLocalAppDataDirPath() {
    // Get the path to the Local AppData folder
    wchar_t szPath[MAX_PATH] = { 0 };
    SHGetFolderPath(nullptr, CSIDL_LOCAL_APPDATA, nullptr, SHGFP_TYPE_CURRENT, szPath);
    std::filesystem::path settingsDirPath = szPath;
    settingsDirPath.append(AT_PRODUCT_NAMEW);
    if (!std::filesystem::exists(settingsDirPath)) {
        if (!std::filesystem::create_directory(settingsDirPath)) {
            throw std::exception("Failed to create AltTab directory in APPDATA");
        }
    }
    return settingsDirPath.wstring();
}

std::wstring ATApplicationDirPath() {
    wchar_t szPath[MAX_PATH] = { 0 };
    GetModuleFileName(nullptr, szPath, MAX_PATH);
    std::filesystem::path appDirPath = szPath;
    return appDirPath.parent_path().wstring();
}

// ----------------------------------------------------------------------------
// AltTab settings file path
// ----------------------------------------------------------------------------
std::wstring ATSettingsFilePath(bool overwrite) {
    AT_LOG_INFO("overwrite = %d", overwrite);
    std::filesystem::path settingsFilePath = ATApplicationDirPath();
    settingsFilePath.append(SETTINGS_INI_FILENAME);
    if (!std::filesystem::exists(settingsFilePath) || overwrite) {
        std::ofstream fs(settingsFilePath);
        if (!fs.is_open()) {
            throw std::exception("Failed to create AltTab.ini file in APPDATA/AltTab");
        }
        fs << "; -----------------------------------------------------------------------------" << std::endl;
        fs << "; Configuration/settings file for AltTab."                                       << std::endl;
        fs << "; Notes:"                                                                        << std::endl;
        fs << ";   1. Do NOT edit manually if you are not familiar with settings."              << std::endl;
        fs << ";   2. Color Format is RGB(0xAA, 0xBB, 0xCC) => 0xAABBCC, in hex format."        << std::endl;
        fs << ";        0xAA : Red component"                                                   << std::endl;
        fs << ";        0xBB : Green component"                                                 << std::endl;
        fs << ";        0xCC : Blue component"                                                  << std::endl;
        fs << ";   3. FontStyle: normal / italic / bold / bold italic"                          << std::endl;
        fs << ";   4. BacktickKey: Is the hex value of a virtual key code, as defined here:" << std::endl;
        fs << ";      https://learn.microsoft.com/en-us/windows/win32/inputdev/virtual-key-codes" << std::endl;
        fs << ";      You might like to try: 0xC0 (VK_OEM_3) for US Keyboard" << std::endl;
        fs << ";                             0xDC (VK_OEM_5) for Italian Keyboard" << std::endl;
        fs << ";                             0xDE (VK_OEM_7) for French Keyboard" << std::endl;
        fs << ";   5. Please delete this file to create a new settings file when AltTab opens." << std::endl;
        fs << ";   6. Please use tray menu Reload AltTabSettings.ini / Reload in Settings" << std::endl;
        fs << ";      dialog to make use of new settings without restarting AltTab." << std::endl;
        fs << "; -----------------------------------------------------------------------------" << std::endl;
        fs.close();
        ATSettingsToFile(settingsFilePath.wstring());
    }
    return settingsFilePath.wstring();
}

template<typename T>
void WriteSetting(const std::wstring& iniFile, LPCTSTR section, LPCTSTR keyName, const T& value) {
    WritePrivateProfileStringW(section, keyName, std::to_wstring(value).c_str(), iniFile.c_str());
}

template<>
void WriteSetting(const std::wstring& iniFile, LPCTSTR section, LPCTSTR keyName, const std::wstring& value) {
    WritePrivateProfileStringW(section, keyName, value.c_str(), iniFile.c_str());
}

template<typename T, typename DefaultType>
void ReadSetting(const std::wstring& iniFile, LPCTSTR section, LPCTSTR keyName, DefaultType defaultValue, T& value) {
    value = GetPrivateProfileIntW(section, keyName, defaultValue, iniFile.c_str());
}

template<>
void ReadSetting(const std::wstring& iniFile, LPCTSTR section, LPCTSTR keyName, LPCTSTR defaultValue, std::wstring& value) {
    const int bufferSize = 4096; // Initial buffer size
    wchar_t buffer[bufferSize];  // Buffer to store the retrieved string
    GetPrivateProfileStringW(section, keyName, defaultValue, buffer, bufferSize, iniFile.c_str());
    value = buffer;
}
int64_t HexToDecimal(const std::wstring& hexStr) {
    std::wregex hexRegex(L"^(0[xX])?[0-9a-fA-F]+$");
    if (!std::regex_match(hexStr, hexRegex)) {
        AT_LOG_DEBUG("%S does not seem to be a hex value", hexStr.c_str());
        return 0;
    }

    const wchar_t* str = hexStr.c_str();

    // Skip "0x" or "0X" prefix if present
    if (hexStr.length() >= 2 && str[0] == L'0' && (str[1] == L'x' || str[1] == L'X')) {
        str += 2;
    }

    wchar_t* endPtr = nullptr;
    uint64_t value = wcstoull(str, &endPtr, 16);

    return value;
}

const wchar_t* DecimalToHex(uint64_t decimal) {
    static wchar_t buffer[32];
    swprintf_s(buffer, L"0x%llX", decimal);
    return buffer;
}


/*!
 * \brief Write the current settings the given file path
 * 
 * \param iniFile    AltTab settings file path
 */
void ATSettingsToFile(const std::wstring& iniFile) {
    // Convert color values to 0xRRGGBB string format and save in AltTabSettings.ini file
    const std::wstring SSFontColor                = ColorRefToRGBString(g_Settings.SSFontColor               );
    const std::wstring SSBackgroundColor          = ColorRefToRGBString(g_Settings.SSBackgroundColor         );
    const std::wstring LVFontColor                = ColorRefToRGBString(g_Settings.LVFontColor               );
    const std::wstring LVBackgroundColor          = ColorRefToRGBString(g_Settings.LVBackgroundColor         );
    const std::wstring LVHighlightTextColor       = ColorRefToRGBString(g_Settings.LVHighlightTextColor      );
    const std::wstring LVHighlightBackgroundColor = ColorRefToRGBString(g_Settings.LVHighlightBackgroundColor);
    const std::wstring HKBacktickKeyHex = DecimalToHex(g_Settings.HKBacktickKey);

    WriteSetting(iniFile, HOTKEYS           , ALTTAB_ENABLED           , g_Settings.HKAltTabEnabled         );
    WriteSetting(iniFile, HOTKEYS           , ALTBACKTICK_ENABLED      , g_Settings.HKAltBacktickEnabled    );
    WriteSetting(iniFile, HOTKEYS           , ALTCTRLTAB_ENABLED       , g_Settings.HKAltCtrlTabEnabled     );
    WriteSetting(iniFile, HOTKEYS           , BACKTICK_KEY             , HKBacktickKeyHex);

    WriteSetting(iniFile, SEARCH_STRING     , CUE_BANNER_TEXT          , g_Settings.SSCueBannerText         );
    WriteSetting(iniFile, SEARCH_STRING     , FONT_NAME                , g_Settings.SSFontName              );
    WriteSetting(iniFile, SEARCH_STRING     , FONT_SIZE                , g_Settings.SSFontSize              );
    WriteSetting(iniFile, SEARCH_STRING     , FONT_STYLE               , g_Settings.SSFontStyle             );
    WriteSetting(iniFile, SEARCH_STRING     , FONT_COLOR               , SSFontColor                        );
    WriteSetting(iniFile, SEARCH_STRING     , BACKGROUND_COLOR         , SSBackgroundColor                  );
    WriteSetting(iniFile, LIST_VIEW         , FONT_NAME                , g_Settings.LVFontName              );
    WriteSetting(iniFile, LIST_VIEW         , FONT_SIZE                , g_Settings.LVFontSize              );
    WriteSetting(iniFile, LIST_VIEW         , FONT_STYLE               , g_Settings.LVFontStyle             );
    WriteSetting(iniFile, LIST_VIEW         , FONT_COLOR               , LVFontColor                        );
    WriteSetting(iniFile, LIST_VIEW         , BACKGROUND_COLOR         , LVBackgroundColor                  );
    WriteSetting(iniFile, LIST_VIEW         , HIGHLIGHT_TEXT_COLOR     , LVHighlightTextColor               );
    WriteSetting(iniFile, LIST_VIEW         , HIGHLIGHT_BG_COLOR       , LVHighlightBackgroundColor         );
    WriteSetting(iniFile, GENERAL           , PROMPT_TERMINATE_ALL     , g_Settings.PromptTerminateAll      );
    WriteSetting(iniFile, GENERAL           , FUZZY_MATCH_PERCENT      , g_Settings.FuzzyMatchPercent       );
    WriteSetting(iniFile, GENERAL           , WINDOW_TRANSPARENCY      , g_Settings.Transparency            );
    WriteSetting(iniFile, GENERAL           , WINDOW_WIDTH_PERCENTAGE  , g_Settings.WidthPercentage         );
    WriteSetting(iniFile, GENERAL           , WINDOW_HEIGHT_PERCENTAGE , g_Settings.HeightPercentage        );
    WriteSetting(iniFile, GENERAL           , SHOW_SEARCH_STRING       , g_Settings.ShowSearchString        );
    WriteSetting(iniFile, GENERAL           , SHOW_COL_HEADER          , g_Settings.ShowColHeader           );
    WriteSetting(iniFile, GENERAL           , SHOW_COL_PROCESSNAME     , g_Settings.ShowColProcessName      );
    WriteSetting(iniFile, GENERAL           , CHECK_FOR_UPDATES        , g_Settings.CheckForUpdatesOpt      );
    WriteSetting(iniFile, GENERAL           , SYSTEM_TRAY_ICON_ENABLED , g_Settings.SystemTrayIconEnabled   );
    WriteSetting(iniFile, MOUSE_HOVER       , SHOW_PROCESS_INFO_TOOLTIP, g_Settings.ShowProcessInfoTooltip  );
    WriteSetting(iniFile, MOUSE_HOVER       , SHOW_HIGHLIGHT_RECT      , g_Settings.ShowHighlightRect       );
    WriteSetting(iniFile, MOUSE_HOVER       , SHOW_DELETE_BUTTON       , g_Settings.ShowDeleteButton        );
    WriteSetting(iniFile, BACKTICK          , SIMILAR_PROCESS_GROUPS   , g_Settings.SimilarProcessGroups    );
    WriteSetting(iniFile, PROCESS_EXCLUSIONS, ENABLED                  , g_Settings.ProcessExclusionsEnabled);
    WriteSetting(iniFile, PROCESS_EXCLUSIONS, PROCESS_LIST             , g_Settings.ProcessExclusions       );
}

/*!
 * \brief Load settings from the settings file path
 */
void ATLoadSettings() {
    AT_LOG_TRACE;
    const std::wstring iniFile = ATSettingsFilePath();
    AT_LOG_INFO("Loading settings from file: [%ls]", iniFile.c_str());

    DWORD SSFontColor                = 0;
    DWORD SSBackgroundColor          = 0;
    DWORD LVFontColor                = 0;
    DWORD LVBackgroundColor          = 0;
    DWORD LVHighlightTextColor       = 0;
    DWORD LVHighlightBackgroundColor = 0;
    std::wstring HKBacktickKeyHex;

    // Setting default values here seems a bit silly if we can just call `g_Settings.Reset();` instead. That way
    // defaults are in one place.
    ReadSetting(iniFile, HOTKEYS           , ALTTAB_ENABLED           , DEFAULT_ALT_TAB_ENABLED            , g_Settings.HKAltTabEnabled         );
    ReadSetting(iniFile, HOTKEYS           , ALTBACKTICK_ENABLED      , DEFAULT_ALT_BACKTICK_ENABLED       , g_Settings.HKAltBacktickEnabled    );
    ReadSetting(iniFile, HOTKEYS           , ALTCTRLTAB_ENABLED       , DEFAULT_ALT_CTRL_TAB_ENABLED       , g_Settings.HKAltCtrlTabEnabled     );
    ReadSetting(iniFile, HOTKEYS           , BACKTICK_KEY             , DEFAULT_BACKTICK_KEY               , HKBacktickKeyHex);
    ReadSetting(iniFile, SEARCH_STRING     , CUE_BANNER_TEXT          , DEFAULT_SS_CUE_BANNER_TEXT         , g_Settings.SSCueBannerText         );
    ReadSetting(iniFile, SEARCH_STRING     , FONT_NAME                , DEFAULT_SS_FONT_NAME               , g_Settings.SSFontName              );
    ReadSetting(iniFile, SEARCH_STRING     , FONT_SIZE                , DEFAULT_SS_FONT_SIZE               , g_Settings.SSFontSize              );
    ReadSetting(iniFile, SEARCH_STRING     , FONT_STYLE               , DEFAULT_SS_FONT_STYLE              , g_Settings.SSFontStyle             );
    ReadSetting(iniFile, SEARCH_STRING     , FONT_COLOR               , DEFAULT_SS_FONT_COLOR              , SSFontColor                        );
    ReadSetting(iniFile, SEARCH_STRING     , BACKGROUND_COLOR         , DEFAULT_SS_BG_COLOR                , SSBackgroundColor                  );
    ReadSetting(iniFile, LIST_VIEW         , FONT_NAME                , DEFAULT_LV_FONT_NAME               , g_Settings.LVFontName              );
    ReadSetting(iniFile, LIST_VIEW         , FONT_SIZE                , DEFAULT_LV_FONT_SIZE               , g_Settings.LVFontSize              );
    ReadSetting(iniFile, LIST_VIEW         , FONT_STYLE               , DEFAULT_LV_FONT_STYLE              , g_Settings.LVFontStyle             );
    ReadSetting(iniFile, LIST_VIEW         , FONT_COLOR               , DEFAULT_LV_FONT_COLOR              , LVFontColor                        );
    ReadSetting(iniFile, LIST_VIEW         , BACKGROUND_COLOR         , DEFAULT_LV_BG_COLOR                , LVBackgroundColor                  );
    ReadSetting(iniFile, LIST_VIEW         , HIGHLIGHT_TEXT_COLOR     , DEFAULT_LV_HIGHLIGHT_TEXT_COLOR    , LVHighlightTextColor               );
    ReadSetting(iniFile, LIST_VIEW         , HIGHLIGHT_BG_COLOR       , DEFAULT_LV_HIGHLIGHT_BG_COLOR      , LVHighlightBackgroundColor         );
    ReadSetting(iniFile, BACKTICK          , SIMILAR_PROCESS_GROUPS   , DEFAULT_SIMILARPROCESSGROUPS       , g_Settings.SimilarProcessGroups    );
    ReadSetting(iniFile, GENERAL           , PROMPT_TERMINATE_ALL     , DEFAULT_PROMPTTERMINATEALL         , g_Settings.PromptTerminateAll      );
    ReadSetting(iniFile, GENERAL           , FUZZY_MATCH_PERCENT      , DEFAULT_FUZZYMATCHPERCENT          , g_Settings.FuzzyMatchPercent       );
    ReadSetting(iniFile, GENERAL           , WINDOW_TRANSPARENCY      , DEFAULT_TRANSPARENCY               , g_Settings.Transparency            );
    ReadSetting(iniFile, GENERAL           , WINDOW_WIDTH_PERCENTAGE  , DEFAULT_WIDTH                      , g_Settings.WidthPercentage         );
    ReadSetting(iniFile, GENERAL           , WINDOW_HEIGHT_PERCENTAGE , DEFAULT_HEIGHT                     , g_Settings.HeightPercentage        );
    ReadSetting(iniFile, GENERAL           , SHOW_SEARCH_STRING       , DEFAULT_SHOW_SEARCH_STRING         , g_Settings.ShowSearchString        );
    ReadSetting(iniFile, GENERAL           , SHOW_COL_HEADER          , DEFAULT_SHOW_COL_HEADER            , g_Settings.ShowColHeader           );
    ReadSetting(iniFile, GENERAL           , SHOW_COL_PROCESSNAME     , DEFAULT_SHOW_COL_PROCESSNAME       , g_Settings.ShowColProcessName      );
    ReadSetting(iniFile, GENERAL           , CHECK_FOR_UPDATES        , DEFAULT_CHECKFORUPDATES            , g_Settings.CheckForUpdatesOpt      );
    ReadSetting(iniFile, GENERAL           , SYSTEM_TRAY_ICON_ENABLED , DEFAULT_SYSTEM_TRAY_ICON_ENABLED   , g_Settings.SystemTrayIconEnabled   );
    ReadSetting(iniFile, MOUSE_HOVER       , SHOW_PROCESS_INFO_TOOLTIP, DEFAULT_MH_SHOW_PROCESSINFO_TOOLTIP, g_Settings.ShowProcessInfoTooltip  );
    ReadSetting(iniFile, MOUSE_HOVER       , SHOW_HIGHLIGHT_RECT      , DEFAULT_MH_SHOW_HIGHLIGHT_RECT     , g_Settings.ShowHighlightRect       );
    ReadSetting(iniFile, MOUSE_HOVER       , SHOW_DELETE_BUTTON       , DEFAULT_MH_SHOW_DELETE_BUTTON      , g_Settings.ShowDeleteButton        );
    ReadSetting(iniFile, PROCESS_EXCLUSIONS, ENABLED                  , DEFAULT_PROCESS_EXCLUSIONS_ENABLED , g_Settings.ProcessExclusionsEnabled);
    ReadSetting(iniFile, PROCESS_EXCLUSIONS, PROCESS_LIST             , DEFAULT_PROCESS_EXCLUSIONS         , g_Settings.ProcessExclusions       );

    // Covert color values (0xRRGGBB that are stored in AltTabSettings.ini file) to COLORREF
    g_Settings.SSFontColor                = RGBIntToColorRef(SSFontColor               );
    g_Settings.SSBackgroundColor          = RGBIntToColorRef(SSBackgroundColor         );
    g_Settings.LVFontColor                = RGBIntToColorRef(LVFontColor               );
    g_Settings.LVBackgroundColor          = RGBIntToColorRef(LVBackgroundColor         );
    g_Settings.LVHighlightTextColor       = RGBIntToColorRef(LVHighlightTextColor      );
    g_Settings.LVHighlightBackgroundColor = RGBIntToColorRef(LVHighlightBackgroundColor);
    g_Settings.HKBacktickKey              = HexToDecimal(HKBacktickKeyHex);
    if (g_Settings.HKBacktickKey == 0) {
        AT_LOG_WARN(
            "%ls=%ls is not valid hex. Defaulting to %ls",
            BACKTICK_KEY,
            HKBacktickKeyHex.c_str(),
            DEFAULT_BACKTICK_KEY);
        g_Settings.HKBacktickKey = HexToDecimal(DEFAULT_BACKTICK_KEY);
    }
    // Clear the previous ProcessGroupsList
    g_Settings.ProcessGroupsList.clear();

    auto vs = Split(g_Settings.SimilarProcessGroups, L"|");
    for (auto& item : vs) {
        auto processes = Split(item, L"/");
        for (auto& processName : processes)
            processName = ToLower(processName);
        g_Settings.ProcessGroupsList.emplace_back(processes.begin(), processes.end());
    }

    // Process ProcessExclusions
    // Always split and convert to lower case, then it is easy while checking
    g_Settings.ProcessExclusionList.clear();
    g_Settings.ProcessExclusionList = Split(ToLower(g_Settings.ProcessExclusions), L"/");

    // Initialize additional settings
    g_AltBacktickWndInfo.hWnd   = nullptr;
    g_AltBacktickWndInfo.hOwner = nullptr;

#ifdef _DEBUG
    ATLogSettings(g_Settings);
#endif // _DEBUG
}

/*!
 * \brief Save current settings to the settings ini file path.
 */
void ATSaveSettings() {
    AT_LOG_TRACE;
    ATSettingsToFile(ATSettingsFilePath(true));
}

/*!
 * \brief Get the text of the given dialog item. Actually this is the wrapper on GetDlgItemText
 * 
 * \param hDlg          Dialog handle
 * \param nIDDlgItem    Dialog item
 * 
 * \return Dialog item text in std::wstring.
 */
std::wstring GetDlgItemTextEx(HWND hDlg, int nIDDlgItem) {
    int      textLength = GetWindowTextLength(GetDlgItem(hDlg, nIDDlgItem));
    wchar_t* buffer     = new wchar_t[textLength + 1];
    GetDlgItemTextW(hDlg, nIDDlgItem, buffer, textLength + 1);
    std::wstring result = buffer;
    delete[] buffer;
    return result;
}

/*!
 * \brief Save the given settings to the AltTab settings ini file and load the
 * modified settings to application (g_Settings)
 * 
 * \param[in] hDlg        AltTab settings dialog handle
 */
void ATApplySettings(HWND hDlg) {
    AT_LOG_TRACE;

    // Read settings from UI
    // Since, we are not showing all the settings in the settings dialog, we 
    // need to copy the existing settings, then all the settings will be properly
    // copied to the AltTabSettings.ini file.
    AltTabSettings settings(g_Settings);
    ATReadSettingsFromUI(hDlg, settings);
#ifdef _DEBUG
    ATLogSettings(settings);
#endif // _DEBUG

    // Check if the settings are valid
    bool isValid = false;
    auto errorInfo = settings.IsValid(isValid);
    if (!isValid) {
        MessageBox(hDlg, errorInfo.second.c_str(), errorInfo.first.c_str(), MB_ICONERROR | MB_OK);
        return;
    }

    g_Settings = settings;

    // Save settings
    ATSaveSettings();

    // Load settings to reconstruct the ProcessGroupsList
    ATLoadSettings();

    // Disable Apply button after saving settings
    EnableWindow(GetDlgItem(hDlg, IDC_BUTTON_APPLY), false);
}

/**
 * \brief Read settings from UI
 * 
 * \param[in]  hDlg        AltTab settings dialog handle
 * \param[out] settings    AltTabSettings
 */
void ATReadSettingsFromUI(HWND hDlg, AltTabSettings& settings) {
    settings.SSCueBannerText           = GetDlgItemTextEx  (hDlg, IDC_EDIT_SS_BANNER_TEXT              );
    settings.SimilarProcessGroups      = GetDlgItemTextEx  (hDlg, IDC_EDIT_SIMILAR_PROCESS_GROUPS      );
    settings.FuzzyMatchPercent         = GetDlgItemInt     (hDlg, IDC_EDIT_FUZZY_MATCH_PERCENT     , nullptr, FALSE);
    settings.Transparency              = GetDlgItemInt     (hDlg, IDC_EDIT_WINDOW_TRANSPARENCY     , nullptr, FALSE);
    settings.WidthPercentage           = GetDlgItemInt     (hDlg, IDC_EDIT_WINDOW_WIDTH_PERCENTAGE , nullptr, FALSE);
    settings.HeightPercentage          = GetDlgItemInt     (hDlg, IDC_EDIT_WINDOW_HEIGHT_PERCENTAGE, nullptr, FALSE);
    settings.PromptTerminateAll        = IsDlgButtonChecked(hDlg, IDC_CHECK_PROMPT_TERMINATE_ALL       ) == BST_CHECKED;
    settings.ShowSearchString          = IsDlgButtonChecked(hDlg, IDC_CHECK_SHOW_SEARCH_STRING         ) == BST_CHECKED;
    settings.ShowColHeader             = IsDlgButtonChecked(hDlg, IDC_CHECK_SHOW_COL_HEADER            ) == BST_CHECKED;
    settings.ShowColProcessName        = IsDlgButtonChecked(hDlg, IDC_CHECK_SHOW_COL_PROCESSNAME       ) == BST_CHECKED;
    settings.HKAltTabEnabled           = IsDlgButtonChecked(hDlg, IDC_CHECK_ALT_TAB                    ) == BST_CHECKED;
    settings.HKAltBacktickEnabled      = IsDlgButtonChecked(hDlg, IDC_CHECK_ALT_BACKTICK               ) == BST_CHECKED;
    settings.HKAltCtrlTabEnabled       = IsDlgButtonChecked(hDlg, IDC_CHECK_ALT_CTRL_TAB               ) == BST_CHECKED;
    settings.ShowProcessInfoTooltip    = IsDlgButtonChecked(hDlg, IDC_CHECK_MH_SHOW_PROCESSINFO_TOOLTIP) == BST_CHECKED;
    settings.ShowHighlightRect         = IsDlgButtonChecked(hDlg, IDC_CHECK_MH_SHOW_HIGHLIGHT_RECT     ) == BST_CHECKED;
    settings.ShowDeleteButton          = IsDlgButtonChecked(hDlg, IDC_CHECK_MH_SHOW_DELETE_BUTTON      ) == BST_CHECKED;
    settings.ProcessExclusionsEnabled  = IsDlgButtonChecked(hDlg, IDC_CHECK_PROCESS_EXCLUSIONS         ) == BST_CHECKED;
    settings.ProcessExclusions         = GetDlgItemTextEx  (hDlg, IDC_EDIT_PROCESS_EXCLUSIONS          );
    const int selectedIndex            = ComboBox_GetCurSel(GetDlgItem(hDlg, IDC_CHECK_FOR_UPDATES));
    settings.CheckForUpdatesOpt        = AltTabSettings::CheckForUpdatesOptions[max(selectedIndex, 0)];
}

/**
 * \brief Log AltTab settings
 * 
 * \param[in] settings    AltTabSettings
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
    AT_LOG_DEBUG("  LVHighlightTextColor      : [%s]", WStrToUTF8(ColorRefToRGBString(settings.LVHighlightTextColor)).c_str());
    AT_LOG_DEBUG("  LVHighlightBackgroundColor: [%s]", WStrToUTF8(ColorRefToRGBString(settings.LVHighlightBackgroundColor)).c_str());
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
    AT_LOG_DEBUG("  ShowMouseOverItem         : [%s]", BOOL_TO_CSTR(settings.ShowHighlightRect));
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
    UNREFERENCED_PARAMETER(settings);
#endif // _AT_LOGGER
}

/*!
 * \brief Check if the settings are modified.
 * 
 * \param[in]  hDlg  AltTab settings dialog handle
 * 
 * \return true if the settings are modified, false otherwise
 */
bool AreSettingsModified(HWND hDlg) {
    AltTabSettings settings;
    ATReadSettingsFromUI(hDlg, settings);

    //ATLogSettings(settings);

    bool modified =
        settings.FuzzyMatchPercent        != g_Settings.FuzzyMatchPercent        ||
        settings.Transparency             != g_Settings.Transparency             ||
        settings.WidthPercentage          != g_Settings.WidthPercentage          ||
        settings.HeightPercentage         != g_Settings.HeightPercentage         ||
        settings.CheckForUpdatesOpt       != g_Settings.CheckForUpdatesOpt       ||
        settings.PromptTerminateAll       != g_Settings.PromptTerminateAll       ||
        settings.ShowSearchString         != g_Settings.ShowSearchString         ||
        settings.ShowColHeader            != g_Settings.ShowColHeader            ||
        settings.ShowColProcessName       != g_Settings.ShowColProcessName       ||
        settings.HKAltTabEnabled          != g_Settings.HKAltTabEnabled          ||
        settings.HKAltBacktickEnabled     != g_Settings.HKAltBacktickEnabled     ||
        settings.HKAltCtrlTabEnabled      != g_Settings.HKAltCtrlTabEnabled      ||
        settings.ProcessExclusionsEnabled != g_Settings.ProcessExclusionsEnabled ||
        settings.ProcessExclusions        != g_Settings.ProcessExclusions        ||
        settings.SimilarProcessGroups     != g_Settings.SimilarProcessGroups     ||
        settings.SSCueBannerText          != g_Settings.SSCueBannerText          ||
        // Mouse Hover settings
        settings.ShowProcessInfoTooltip   != g_Settings.ShowProcessInfoTooltip   ||
        settings.ShowHighlightRect        != g_Settings.ShowHighlightRect        ||
        settings.ShowDeleteButton         != g_Settings.ShowDeleteButton         ||
        false;

    return modified;
}

/**
 * \brief Available options of CheckForUpdates
 */
StringList AltTabSettings::CheckForUpdatesOptions = { L"Startup", L"Daily", L"Weekly", L"Never" };

/**
 * \brief Check if the given settings are valid.
 * 
 * \param[out]  valid      Will be set to true if settings are valid otherwise false.
 * 
 * \return Returns a pair of strings. First string is the title of the error message box and the second string is the error message.
 */
std::pair<std::wstring, std::wstring> AltTabSettings::IsValid(bool& valid) {
    const std::wregex pattern(L"^[^\\/:*?\"<>|]+.exe$");

    // Check similar process groups
    auto vs = Split(SimilarProcessGroups, L"|");
    for (int i = 0; i < vs.size(); ++i) {
        auto processes = Split(vs[i], L"/");
        for (auto& processName : processes) {
            if (!std::regex_match(processName, pattern)) {
                valid = false;
                return {
                    L"Invalid Similar Process Groups",
                    std::format(L"Similar Process Groups text contains invalid characters.\n"
                                 "A file name should not contain any of the following characters: \\ / : * ? \" < > | and ends with .exe.\n"
                                 "Found an invalid process name [{}] in group {}, please verify...", processName, i + 1)
                };
            }
        }
    }

    // Check exclude process list
    auto excludeProcessNames = Split(ProcessExclusions, L"/");
    for (auto& processName : excludeProcessNames) {
        if (!std::regex_match(processName, pattern)) {
            valid = false;
            return {
                L"Invalid Process Exclusions",
                std::format(L"Invalid process name [{}] in Process Exclusions, please verify..."
                "A file name should not contain any of the following characters: \\ / : * ? \" < > | and ends with .exe.", processName)
            };
        }
    }

    valid = true;
    return {};
}

/*!
 * \brief Initialize AltTab settings dialog controls with the given settings.
 * 
 * \param hDlg       AltTab settings dialog handle
 * \param settings   AltTabSettings
 */
void ATSettingsInitDialog(HWND hDlg, const AltTabSettings& settings) {
    SetDlgItemText    (hDlg, IDC_EDIT_SETTINGS_FILEPATH       , ATSettingsFilePath().c_str()         );
    SetDlgItemText    (hDlg, IDC_EDIT_SS_BANNER_TEXT          , settings.SSCueBannerText.c_str()     );
    SetDlgItemText    (hDlg, IDC_EDIT_SIMILAR_PROCESS_GROUPS  , settings.SimilarProcessGroups.c_str());
 
    // TODO: Probably not cleaned up
    HFONT    hFont     = CreateFontW(14, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                                     DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                                     DEFAULT_QUALITY, DEFAULT_PITCH | FF_SWISS, L"Lucida Console");
    HWND     hEditBox1 = GetDlgItem(hDlg, IDC_EDIT_SIMILAR_PROCESS_GROUPS);
    HWND     hEditBox2 = GetDlgItem(hDlg, IDC_EDIT_PROCESS_EXCLUSIONS);
 
    SendMessageW(hEditBox1, WM_SETFONT     , (WPARAM)hFont    , TRUE);
    SendMessageW(hEditBox2, WM_SETFONT     , (WPARAM)hFont    , TRUE);
 
    CheckDlgButton    (hDlg, IDC_CHECK_ALT_TAB                    , settings.HKAltTabEnabled          ? BST_CHECKED : BST_UNCHECKED);
    CheckDlgButton    (hDlg, IDC_CHECK_ALT_BACKTICK               , settings.HKAltBacktickEnabled     ? BST_CHECKED : BST_UNCHECKED);
    CheckDlgButton    (hDlg, IDC_CHECK_ALT_CTRL_TAB               , settings.HKAltCtrlTabEnabled      ? BST_CHECKED : BST_UNCHECKED);
    CheckDlgButton    (hDlg, IDC_CHECK_PROMPT_TERMINATE_ALL       , settings.PromptTerminateAll       ? BST_CHECKED : BST_UNCHECKED);
    CheckDlgButton    (hDlg, IDC_CHECK_SHOW_SEARCH_STRING         , settings.ShowSearchString         ? BST_CHECKED : BST_UNCHECKED);
    CheckDlgButton    (hDlg, IDC_CHECK_SHOW_COL_HEADER            , settings.ShowColHeader            ? BST_CHECKED : BST_UNCHECKED);
    CheckDlgButton    (hDlg, IDC_CHECK_SHOW_COL_PROCESSNAME       , settings.ShowColProcessName       ? BST_CHECKED : BST_UNCHECKED);
    CheckDlgButton    (hDlg, IDC_CHECK_PROCESS_EXCLUSIONS         , settings.ProcessExclusionsEnabled ? BST_CHECKED : BST_UNCHECKED);
    CheckDlgButton    (hDlg, IDC_CHECK_MH_SHOW_PROCESSINFO_TOOLTIP, settings.ShowProcessInfoTooltip   ? BST_CHECKED : BST_UNCHECKED);
    CheckDlgButton    (hDlg, IDC_CHECK_MH_SHOW_HIGHLIGHT_RECT     , settings.ShowHighlightRect        ? BST_CHECKED : BST_UNCHECKED);
    CheckDlgButton    (hDlg, IDC_CHECK_MH_SHOW_DELETE_BUTTON      , settings.ShowDeleteButton         ? BST_CHECKED : BST_UNCHECKED);

    EnableWindow      (GetDlgItem(hDlg, IDC_EDIT_PROCESS_EXCLUSIONS), settings.ProcessExclusionsEnabled);
 
    SetDlgItemInt     (hDlg, IDC_EDIT_FUZZY_MATCH_PERCENT      , settings.FuzzyMatchPercent  , FALSE);
    SendDlgItemMessage(hDlg, IDC_SPIN_FUZZY_MATCH_PERCENT      , UDM_SETRANGE                , 0, MAKELPARAM(100, 0));
    SendDlgItemMessage(hDlg, IDC_SPIN_FUZZY_MATCH_PERCENT      , UDM_SETPOS                  , 0, MAKELPARAM(settings.FuzzyMatchPercent, 0));
 
    SetDlgItemInt     (hDlg, IDC_EDIT_WINDOW_TRANSPARENCY      , settings.Transparency       , FALSE);
    SendDlgItemMessage(hDlg, IDC_SPIN_WINDOW_TRANSPARENCY      , UDM_SETRANGE                , 0, MAKELPARAM(255, 0));
    SendDlgItemMessage(hDlg, IDC_SPIN_WINDOW_TRANSPARENCY      , UDM_SETPOS                  , 0, MAKELPARAM(settings.Transparency, 0));
 
    SetDlgItemInt     (hDlg, IDC_EDIT_WINDOW_WIDTH_PERCENTAGE  , settings.WidthPercentage    , FALSE);
    SendDlgItemMessage(hDlg, IDC_SPIN_WINDOW_WIDTH_PERCENTAGE  , UDM_SETRANGE                , 0, MAKELPARAM(90, 10));
    SendDlgItemMessage(hDlg, IDC_SPIN_WINDOW_WIDTH_PERCENTAGE  , UDM_SETPOS                  , 0, MAKELPARAM(settings.WidthPercentage, 0));
 
    SetDlgItemInt     (hDlg, IDC_EDIT_WINDOW_HEIGHT_PERCENTAGE , settings.HeightPercentage   , FALSE);
    SendDlgItemMessage(hDlg, IDC_SPIN_WINDOW_HEIGHT_PERCENTAGE , UDM_SETRANGE                , 0, MAKELPARAM(90, 10));
    SendDlgItemMessage(hDlg, IDC_SPIN_WINDOW_HEIGHT_PERCENTAGE , UDM_SETPOS                  , 0, MAKELPARAM(settings.HeightPercentage, 0));
 
    SetDlgItemText    (hDlg, IDC_EDIT_PROCESS_EXCLUSIONS       , settings.ProcessExclusions.c_str());
 
    HWND hComboBox = GetDlgItem(hDlg, IDC_CHECK_FOR_UPDATES);
    for (auto& opt : AltTabSettings::CheckForUpdatesOptions) {
        ComboBox_AddString(hComboBox, opt.c_str());
    }
    ComboBox_SetCurSel(hComboBox, settings.GetCheckForUpdatesIndex());
 
    // Center the dialog on the screen
    const int screenWidth  = GetSystemMetrics(SM_CXSCREEN);
    const int screenHeight = GetSystemMetrics(SM_CYSCREEN);
    RECT dlgRect;
    GetWindowRect(hDlg, &dlgRect);
 
    const int dlgWidth  = dlgRect.right  - dlgRect.left;
    const int dlgHeight = dlgRect.bottom - dlgRect.top;
 
    const int posX      = (screenWidth  - dlgWidth ) / 2;
    const int posY      = (screenHeight - dlgHeight) / 2;
 
    SetWindowPos(hDlg, HWND_TOP, posX, posY, 0, 0, SWP_NOSIZE);
 
    // Set the dialog as an app window, otherwise not displayed in task bar
    SetWindowLong(hDlg, GWL_EXSTYLE, GetWindowLong(hDlg, GWL_EXSTYLE) | WS_EX_APPWINDOW);
 
    // Needs to be called after the dialog is shown
    bool settingsModified = AreSettingsModified(hDlg);
    EnableWindow(GetDlgItem(hDlg, IDC_BUTTON_APPLY), settingsModified);

    // Create bold font and use it for the Group Box static text
    HFONT hBoldFont = (HFONT)SendMessageW(hDlg, WM_GETFONT, 0, 0);
    LOGFONT lf = {};
    GetObjectW(hBoldFont, sizeof(LOGFONT), &lf);
    lf.lfWeight = FW_BOLD;

    // TODO: Probably not cleaned up
    hBoldFont = CreateFontIndirectW(&lf);

    SendMessageW(GetDlgItem(hDlg, IDC_GROUPBOX_STORAGE           ), WM_SETFONT, (WPARAM)hBoldFont, TRUE);
    SendMessageW(GetDlgItem(hDlg, IDC_GROUPBOX_HOTKEYS           ), WM_SETFONT, (WPARAM)hBoldFont, TRUE);
    SendMessageW(GetDlgItem(hDlg, IDC_GROUPBOX_GENERAL           ), WM_SETFONT, (WPARAM)hBoldFont, TRUE);
    SendMessageW(GetDlgItem(hDlg, IDC_GROUPBOX_MOUSEHOVER        ), WM_SETFONT, (WPARAM)hBoldFont, TRUE);
    SendMessageW(GetDlgItem(hDlg, IDC_GROUPBOX_BACKTICK          ), WM_SETFONT, (WPARAM)hBoldFont, TRUE);
    SendMessageW(GetDlgItem(hDlg, IDC_GROUPBOX_PROCESS_EXCLUSIONS), WM_SETFONT, (WPARAM)hBoldFont, TRUE);
}

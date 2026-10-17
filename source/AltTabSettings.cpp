#include "PreCompile.h"
#include "AltTabSettings.h"
#include "Resource.h"
#include "Utils.h"
#include "Logger.h"
#include "GlobalData.h"
#include "AltTabWindow.h"
#include "version.h"
#include <filesystem>
#include <fstream>
#include <regex>
#include <ShlObj_core.h>

AltTabSettings    g_Settings;
AltTabWindowData  g_AltBacktickWndInfo;

// Static member definition.
StringList AltTabSettings::CheckForUpdatesOptions = { L"Startup", L"Daily", L"Weekly", L"Never" };

namespace {

// INI sections
const wchar_t* SEARCH_STRING             = L"SearchString"            ;
const wchar_t* LIST_VIEW                 = L"ListView"                ;
const wchar_t* GENERAL                   = L"General"                 ;
const wchar_t* HOTKEYS                   = L"Hotkeys"                 ;
const wchar_t* MOUSE_HOVER               = L"MouseHover"              ;
const wchar_t* BACKTICK                  = L"Backtick"                ;
const wchar_t* PROCESS_EXCLUSIONS        = L"ProcessExclusions"       ;

// INI keys
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
const wchar_t* BACKTICK_KEY              = L"BacktickKey"             ;
const wchar_t* SIMILAR_PROCESS_GROUPS    = L"SimilarProcessGroups"    ;
const wchar_t* ENABLED                   = L"Enabled"                 ;
const wchar_t* PROCESS_LIST              = L"ProcessList"             ;

// Rebuild ProcessGroupsList / ProcessExclusionList from their string versions.
void BuildProcessLists(AltTabSettings& settings) {
    settings.ProcessGroupsList.clear();
    const auto groups = Split(settings.SimilarProcessGroups, L"|");
    for (auto& group : groups) {
        auto processes = Split(group, L"/");
        for (auto& processName : processes) {
            processName = ToLower(processName);
        }
        settings.ProcessGroupsList.emplace_back(processes.begin(), processes.end());
    }

    settings.ProcessExclusionList.clear();
    settings.ProcessExclusionList = Split(ToLower(settings.ProcessExclusions), L"/");
}

// Convert COLORREF to a 0xRRGGBB string.
std::wstring ColorRefToRGBStringImpl(COLORREF colorRef) {
    wchar_t buffer[9];
    swprintf_s(buffer, 9, L"0x%02X%02X%02X", GetRValue(colorRef), GetGValue(colorRef), GetBValue(colorRef));
    return buffer;
}

// Build a COLORREF from an RGB integer value (0xRRGGBB).
COLORREF RGBIntToColorRefImpl(int hexValue) {
    const int red   = (hexValue >> 16) & 0xFF;
    const int green = (hexValue >> 8) & 0xFF;
    const int blue  = (hexValue)&0xFF;
    return RGB(red, green, blue);
}

int64_t HexToDecimal(const std::wstring& hexStr) {
    static const std::wregex hexRegex(L"^(0[xX])?[0-9a-fA-F]+$");
    if (!std::regex_match(hexStr, hexRegex)) {
        AT_LOG_DEBUG("%ls does not seem to be a hex value", hexStr.c_str());
        return 0;
    }

    const wchar_t* str = hexStr.c_str();
    if (hexStr.length() >= 2 && str[0] == L'0' && (str[1] == L'x' || str[1] == L'X')) {
        str += 2;
    }

    return (int64_t)wcstoull(str, nullptr, 16);
}

const wchar_t* DecimalToHex(uint64_t decimal) {
    static wchar_t buffer[32];
    swprintf_s(buffer, L"0x%llX", decimal);
    return buffer;
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
void ReadSetting(
    const std::wstring& iniFile,
    LPCTSTR section,
    LPCTSTR keyName,
    LPCTSTR defaultValue,
    std::wstring& value)
{
    const int bufferSize = 4096;
    wchar_t buffer[bufferSize] = {};
    GetPrivateProfileStringW(section, keyName, defaultValue, buffer, bufferSize, iniFile.c_str());
    value = buffer;
}

} // namespace

// Public wrappers used by the settings dialog / logging.
std::wstring ColorRefToRGBString(COLORREF colorRef) {
    return ColorRefToRGBStringImpl(colorRef);
}

COLORREF RGBIntToColorRef(int hexValue) {
    return RGBIntToColorRefImpl(hexValue);
}

// ----------------------------------------------------------------------------
// AltTabSettings
// ----------------------------------------------------------------------------

AltTabSettings::AltTabSettings() {
    Reset();
}

void AltTabSettings::Reset() {
    HKAltTabEnabled            = DEFAULT_ALT_TAB_ENABLED            ;
    HKAltBacktickEnabled       = DEFAULT_ALT_BACKTICK_ENABLED       ;
    HKAltCtrlTabEnabled        = DEFAULT_ALT_CTRL_TAB_ENABLED       ;
    HKBacktickKey              = (WPARAM)HexToDecimal(DEFAULT_BACKTICK_KEY);
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
    ShowHighlightRect          = DEFAULT_MH_SHOW_HIGHLIGHT_RECT     ;
    ShowDeleteButton           = DEFAULT_MH_SHOW_DELETE_BUTTON      ;
    SystemTrayIconEnabled      = DEFAULT_SYSTEM_TRAY_ICON_ENABLED   ;
    ProcessExclusionsEnabled   = DEFAULT_PROCESS_EXCLUSIONS_ENABLED ;
    ProcessExclusions          = DEFAULT_PROCESS_EXCLUSIONS         ;

    BuildProcessLists(*this);

    g_AltBacktickWndInfo.hWnd   = nullptr;
    g_AltBacktickWndInfo.hOwner = nullptr;
}

void AltTabSettings::Load() {
    ATLoadSettings();
}

void AltTabSettings::Save() {
    ATSaveSettings();
}

int AltTabSettings::GetCheckForUpdatesIndex() const {
    const auto it = std::find(CheckForUpdatesOptions.begin(), CheckForUpdatesOptions.end(), this->CheckForUpdatesOpt);
    if (it == CheckForUpdatesOptions.end()) {
        return 0;
    }
    return (int)std::distance(CheckForUpdatesOptions.begin(), it);
}

std::pair<std::wstring, std::wstring> AltTabSettings::IsValid(bool& valid) {
    const std::wregex pattern(L"^[^\\/:*?\"<>|]+.exe$");

    // Validate similar process groups.
    const auto groups = Split(SimilarProcessGroups, L"|");
    for (size_t i = 0; i < groups.size(); ++i) {
        const auto processes = Split(groups[i], L"/");
        for (const auto& processName : processes) {
            if (!std::regex_match(processName, pattern)) {
                valid = false;
                return {
                    L"Invalid Similar Process Groups",
                    std::format(
                        L"Similar Process Groups text contains invalid characters.\n"
                        "A file name should not contain any of the following characters: \\ / : * ? \" < > | and ends with "
                        ".exe.\n"
                        "Found an invalid process name [{}] in group {}, please verify...",
                        processName,
                        i + 1)
                };
            }
        }
    }

    // Validate the process exclusion list.
    const auto excludeProcessNames = Split(ProcessExclusions, L"/");
    for (const auto& processName : excludeProcessNames) {
        if (!std::regex_match(processName, pattern)) {
            valid = false;
            return {
                L"Invalid Process Exclusions",
                std::format(L"Invalid process name [{}] in Process Exclusions, please verify...", processName)
            };
        }
    }

    valid = true;
    return {};
}

// ----------------------------------------------------------------------------
// Paths
// ----------------------------------------------------------------------------

std::wstring ATLocalAppDataDirPath() {
    wchar_t szPath[MAX_PATH] = {};
    SHGetFolderPath(nullptr, CSIDL_LOCAL_APPDATA, nullptr, SHGFP_TYPE_CURRENT, szPath);
    std::filesystem::path settingsDirPath = szPath;
    settingsDirPath.append(AT_PRODUCT_NAMEW);
    if (!std::filesystem::exists(settingsDirPath)) {
        std::filesystem::create_directory(settingsDirPath);
    }
    return settingsDirPath.wstring();
}

std::wstring ATApplicationDirPath() {
    wchar_t szPath[MAX_PATH] = {};
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
            AT_LOG_ERROR("Failed to create settings file: %ls", settingsFilePath.wstring().c_str());
            return settingsFilePath.wstring();
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
        fs << ";   4. BacktickKey: Is the hex value of a virtual key code, as defined here:"    << std::endl;
        fs << ";      https://learn.microsoft.com/en-us/windows/win32/inputdev/virtual-key-codes" << std::endl;
        fs << ";      You might like to try: 0xC0 (VK_OEM_3) for US Keyboard"                    << std::endl;
        fs << ";                             0xDC (VK_OEM_5) for Italian Keyboard"              << std::endl;
        fs << ";                             0xDE (VK_OEM_7) for French Keyboard"               << std::endl;
        fs << ";   5. Please delete this file to create a new settings file when AltTab opens." << std::endl;
        fs << ";   6. Please use tray menu Reload AltTabSettings.ini / Reload in Settings"       << std::endl;
        fs << ";      dialog to make use of new settings without restarting AltTab."             << std::endl;
        fs << "; -----------------------------------------------------------------------------" << std::endl;
        fs.close();

        ATSettingsToFile(settingsFilePath.wstring());
    }

    return settingsFilePath.wstring();
}

// ----------------------------------------------------------------------------
// Persistence
// ----------------------------------------------------------------------------

void ATSettingsToFile(const std::wstring& iniFile) {
    const std::wstring SSFontColor = ColorRefToRGBString(g_Settings.SSFontColor);
    const std::wstring SSBackgroundColor = ColorRefToRGBString(g_Settings.SSBackgroundColor);
    const std::wstring LVFontColor = ColorRefToRGBString(g_Settings.LVFontColor);
    const std::wstring LVBackgroundColor = ColorRefToRGBString(g_Settings.LVBackgroundColor);
    const std::wstring LVHighlightTextColor = ColorRefToRGBString(g_Settings.LVHighlightTextColor);
    const std::wstring LVHighlightBackgroundColor = ColorRefToRGBString(g_Settings.LVHighlightBackgroundColor);
    const std::wstring HKBacktickKeyHex = DecimalToHex(g_Settings.HKBacktickKey);

    WriteSetting(iniFile, HOTKEYS, ALTTAB_ENABLED, g_Settings.HKAltTabEnabled);
    WriteSetting(iniFile, HOTKEYS, ALTBACKTICK_ENABLED, g_Settings.HKAltBacktickEnabled);
    WriteSetting(iniFile, HOTKEYS, ALTCTRLTAB_ENABLED, g_Settings.HKAltCtrlTabEnabled);
    WriteSetting(iniFile, HOTKEYS, BACKTICK_KEY, HKBacktickKeyHex);

    WriteSetting(iniFile, SEARCH_STRING, CUE_BANNER_TEXT, g_Settings.SSCueBannerText);
    WriteSetting(iniFile, SEARCH_STRING, FONT_NAME, g_Settings.SSFontName);
    WriteSetting(iniFile, SEARCH_STRING, FONT_SIZE, g_Settings.SSFontSize);
    WriteSetting(iniFile, SEARCH_STRING, FONT_STYLE, g_Settings.SSFontStyle);
    WriteSetting(iniFile, SEARCH_STRING, FONT_COLOR, SSFontColor);
    WriteSetting(iniFile, SEARCH_STRING, BACKGROUND_COLOR, SSBackgroundColor);

    WriteSetting(iniFile, LIST_VIEW, FONT_NAME, g_Settings.LVFontName);
    WriteSetting(iniFile, LIST_VIEW, FONT_SIZE, g_Settings.LVFontSize);
    WriteSetting(iniFile, LIST_VIEW, FONT_STYLE, g_Settings.LVFontStyle);
    WriteSetting(iniFile, LIST_VIEW, FONT_COLOR, LVFontColor);
    WriteSetting(iniFile, LIST_VIEW, BACKGROUND_COLOR, LVBackgroundColor);
    WriteSetting(iniFile, LIST_VIEW, HIGHLIGHT_TEXT_COLOR, LVHighlightTextColor);
    WriteSetting(iniFile, LIST_VIEW, HIGHLIGHT_BG_COLOR, LVHighlightBackgroundColor);

    WriteSetting(iniFile, GENERAL, PROMPT_TERMINATE_ALL, g_Settings.PromptTerminateAll);
    WriteSetting(iniFile, GENERAL, FUZZY_MATCH_PERCENT, g_Settings.FuzzyMatchPercent);
    WriteSetting(iniFile, GENERAL, WINDOW_TRANSPARENCY, g_Settings.Transparency);
    WriteSetting(iniFile, GENERAL, WINDOW_WIDTH_PERCENTAGE, g_Settings.WidthPercentage);
    WriteSetting(iniFile, GENERAL, WINDOW_HEIGHT_PERCENTAGE, g_Settings.HeightPercentage);
    WriteSetting(iniFile, GENERAL, SHOW_SEARCH_STRING, g_Settings.ShowSearchString);
    WriteSetting(iniFile, GENERAL, SHOW_COL_HEADER, g_Settings.ShowColHeader);
    WriteSetting(iniFile, GENERAL, SHOW_COL_PROCESSNAME, g_Settings.ShowColProcessName);
    WriteSetting(iniFile, GENERAL, CHECK_FOR_UPDATES, g_Settings.CheckForUpdatesOpt);
    WriteSetting(iniFile, GENERAL, SYSTEM_TRAY_ICON_ENABLED, g_Settings.SystemTrayIconEnabled);

    WriteSetting(iniFile, MOUSE_HOVER, SHOW_PROCESS_INFO_TOOLTIP, g_Settings.ShowProcessInfoTooltip);
    WriteSetting(iniFile, MOUSE_HOVER, SHOW_HIGHLIGHT_RECT, g_Settings.ShowHighlightRect);
    WriteSetting(iniFile, MOUSE_HOVER, SHOW_DELETE_BUTTON, g_Settings.ShowDeleteButton);

    WriteSetting(iniFile, BACKTICK, SIMILAR_PROCESS_GROUPS, g_Settings.SimilarProcessGroups);

    WriteSetting(iniFile, PROCESS_EXCLUSIONS, ENABLED, g_Settings.ProcessExclusionsEnabled);
    WriteSetting(iniFile, PROCESS_EXCLUSIONS, PROCESS_LIST, g_Settings.ProcessExclusions);
}

void ATLoadSettings() {
    AT_LOG_TRACE;
    if (g_Settings.ProcessGroupsList.empty()) {
        // First load: parse groups from defaults before reading values below.
        BuildProcessLists(g_Settings);
    }

    const std::wstring iniFile = ATSettingsFilePath();
    AT_LOG_INFO("Loading settings from file: [%ls]", iniFile.c_str());

    DWORD SSFontColor = 0;
    DWORD SSBackgroundColor = 0;
    DWORD LVFontColor = 0;
    DWORD LVBackgroundColor = 0;
    DWORD LVHighlightTextColor = 0;
    DWORD LVHighlightBackgroundColor = 0;
    std::wstring HKBacktickKeyHex;

    ReadSetting(iniFile, HOTKEYS, ALTTAB_ENABLED, DEFAULT_ALT_TAB_ENABLED, g_Settings.HKAltTabEnabled);
    ReadSetting(iniFile, HOTKEYS, ALTBACKTICK_ENABLED, DEFAULT_ALT_BACKTICK_ENABLED, g_Settings.HKAltBacktickEnabled);
    ReadSetting(iniFile, HOTKEYS, ALTCTRLTAB_ENABLED, DEFAULT_ALT_CTRL_TAB_ENABLED, g_Settings.HKAltCtrlTabEnabled);
    ReadSetting(iniFile, HOTKEYS, BACKTICK_KEY, DEFAULT_BACKTICK_KEY, HKBacktickKeyHex);

    ReadSetting(iniFile, SEARCH_STRING, CUE_BANNER_TEXT, DEFAULT_SS_CUE_BANNER_TEXT, g_Settings.SSCueBannerText);
    ReadSetting(iniFile, SEARCH_STRING, FONT_NAME, DEFAULT_SS_FONT_NAME, g_Settings.SSFontName);
    ReadSetting(iniFile, SEARCH_STRING, FONT_SIZE, DEFAULT_SS_FONT_SIZE, g_Settings.SSFontSize);
    ReadSetting(iniFile, SEARCH_STRING, FONT_STYLE, DEFAULT_SS_FONT_STYLE, g_Settings.SSFontStyle);
    ReadSetting(iniFile, SEARCH_STRING, FONT_COLOR, DEFAULT_SS_FONT_COLOR, SSFontColor);
    ReadSetting(iniFile, SEARCH_STRING, BACKGROUND_COLOR, DEFAULT_SS_BG_COLOR, SSBackgroundColor);

    ReadSetting(iniFile, LIST_VIEW, FONT_NAME, DEFAULT_LV_FONT_NAME, g_Settings.LVFontName);
    ReadSetting(iniFile, LIST_VIEW, FONT_SIZE, DEFAULT_LV_FONT_SIZE, g_Settings.LVFontSize);
    ReadSetting(iniFile, LIST_VIEW, FONT_STYLE, DEFAULT_LV_FONT_STYLE, g_Settings.LVFontStyle);
    ReadSetting(iniFile, LIST_VIEW, FONT_COLOR, DEFAULT_LV_FONT_COLOR, LVFontColor);
    ReadSetting(iniFile, LIST_VIEW, BACKGROUND_COLOR, DEFAULT_LV_BG_COLOR, LVBackgroundColor);
    ReadSetting(iniFile, LIST_VIEW, HIGHLIGHT_TEXT_COLOR, DEFAULT_LV_HIGHLIGHT_TEXT_COLOR, LVHighlightTextColor);
    ReadSetting(iniFile, LIST_VIEW, HIGHLIGHT_BG_COLOR, DEFAULT_LV_HIGHLIGHT_BG_COLOR, LVHighlightBackgroundColor);

    ReadSetting(iniFile, BACKTICK, SIMILAR_PROCESS_GROUPS, DEFAULT_SIMILARPROCESSGROUPS, g_Settings.SimilarProcessGroups);

    ReadSetting(iniFile, GENERAL, PROMPT_TERMINATE_ALL, DEFAULT_PROMPTTERMINATEALL, g_Settings.PromptTerminateAll);
    ReadSetting(iniFile, GENERAL, FUZZY_MATCH_PERCENT, DEFAULT_FUZZYMATCHPERCENT, g_Settings.FuzzyMatchPercent);
    ReadSetting(iniFile, GENERAL, WINDOW_TRANSPARENCY, DEFAULT_TRANSPARENCY, g_Settings.Transparency);
    ReadSetting(iniFile, GENERAL, WINDOW_WIDTH_PERCENTAGE, DEFAULT_WIDTH, g_Settings.WidthPercentage);
    ReadSetting(iniFile, GENERAL, WINDOW_HEIGHT_PERCENTAGE, DEFAULT_HEIGHT, g_Settings.HeightPercentage);
    ReadSetting(iniFile, GENERAL, SHOW_SEARCH_STRING, DEFAULT_SHOW_SEARCH_STRING, g_Settings.ShowSearchString);
    ReadSetting(iniFile, GENERAL, SHOW_COL_HEADER, DEFAULT_SHOW_COL_HEADER, g_Settings.ShowColHeader);
    ReadSetting(iniFile, GENERAL, SHOW_COL_PROCESSNAME, DEFAULT_SHOW_COL_PROCESSNAME, g_Settings.ShowColProcessName);
    ReadSetting(iniFile, GENERAL, CHECK_FOR_UPDATES, DEFAULT_CHECKFORUPDATES, g_Settings.CheckForUpdatesOpt);
    ReadSetting(iniFile, GENERAL, SYSTEM_TRAY_ICON_ENABLED, DEFAULT_SYSTEM_TRAY_ICON_ENABLED, g_Settings.SystemTrayIconEnabled);

    ReadSetting(iniFile, MOUSE_HOVER, SHOW_PROCESS_INFO_TOOLTIP, DEFAULT_MH_SHOW_PROCESSINFO_TOOLTIP, g_Settings.ShowProcessInfoTooltip);
    ReadSetting(iniFile, MOUSE_HOVER, SHOW_HIGHLIGHT_RECT, DEFAULT_MH_SHOW_HIGHLIGHT_RECT, g_Settings.ShowHighlightRect);
    ReadSetting(iniFile, MOUSE_HOVER, SHOW_DELETE_BUTTON, DEFAULT_MH_SHOW_DELETE_BUTTON, g_Settings.ShowDeleteButton);

    ReadSetting(iniFile, PROCESS_EXCLUSIONS, ENABLED, DEFAULT_PROCESS_EXCLUSIONS_ENABLED, g_Settings.ProcessExclusionsEnabled);
    ReadSetting(iniFile, PROCESS_EXCLUSIONS, PROCESS_LIST, DEFAULT_PROCESS_EXCLUSIONS, g_Settings.ProcessExclusions);

    // Convert stored 0xRRGGBB colors to COLORREF.
    g_Settings.SSFontColor = RGBIntToColorRef(SSFontColor);
    g_Settings.SSBackgroundColor = RGBIntToColorRef(SSBackgroundColor);
    g_Settings.LVFontColor = RGBIntToColorRef(LVFontColor);
    g_Settings.LVBackgroundColor = RGBIntToColorRef(LVBackgroundColor);
    g_Settings.LVHighlightTextColor = RGBIntToColorRef(LVHighlightTextColor);
    g_Settings.LVHighlightBackgroundColor = RGBIntToColorRef(LVHighlightBackgroundColor);

    g_Settings.HKBacktickKey = (WPARAM)HexToDecimal(HKBacktickKeyHex);
    if (g_Settings.HKBacktickKey == 0) {
        AT_LOG_WARN(
            "%ls=%ls is not valid hex. Defaulting to %ls",
            BACKTICK_KEY,
            HKBacktickKeyHex.c_str(),
            DEFAULT_BACKTICK_KEY);
        g_Settings.HKBacktickKey = (WPARAM)HexToDecimal(DEFAULT_BACKTICK_KEY);
    }

    BuildProcessLists(g_Settings);

    g_AltBacktickWndInfo.hWnd   = nullptr;
    g_AltBacktickWndInfo.hOwner = nullptr;
}

void ATSaveSettings() {
    AT_LOG_TRACE;
    ATSettingsToFile(ATSettingsFilePath(true));
}

// ----------------------------------------------------------------------------
// Similar process groups
// ----------------------------------------------------------------------------

int GetProcessGroupIndex(const std::wstring& processName) {
    const std::wstring processNameL = ToLower(processName);
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

    const int index = GetProcessGroupIndex(processNameA);
    if (index == -1)
        return false;

    return g_Settings.ProcessGroupsList[index].contains(ToLower(processNameB));
}
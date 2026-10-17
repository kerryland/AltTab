#include "PreCompile.h"
#include "Utils.h"
#include "Logger.h"
#include <algorithm>
#include <comdef.h>
#include <cwctype>
#include <fcntl.h>
#include <gdiplus.h>
#include <io.h>
#include <iostream>
#include <shellapi.h>

#pragma comment(lib, "comsupp.lib")
#pragma comment(lib, "gdiplus.lib")

ULONG_PTR g_gdiplusToken;

bool EnableConsoleWindow() {
    if (!AttachConsole(ATTACH_PARENT_PROCESS)) {
        if (!AllocConsole())
            return false;
    }

    FILE* stdinNew = nullptr;
    freopen_s(&stdinNew, "CONIN$", "r+", stdin);
    FILE* stdoutNew = nullptr;
    freopen_s(&stdoutNew, "CONOUT$", "w+", stdout);
    FILE* stderrNew = nullptr;
    freopen_s(&stderrNew, "CONOUT$", "w+", stderr);

    return true;
}

std::string Trim(const std::string& str, const std::string& seps) {
    if (str.empty())
        return str;
    const size_t p = str.find_first_not_of(seps);
    if (p == std::string::npos)
        return "";
    const size_t q = str.find_last_not_of(seps);
    return str.substr(p, q - p + 1);
}

std::vector<std::string> Split(const std::string& s, const std::string& seps) {
    std::vector<std::string> ret;
    for (size_t p = 0, q; p != std::string::npos; p = q) {
        p = s.find_first_not_of(seps, p);
        if (p == std::string::npos)
            break;
        q = s.find_first_of(seps, p);
        ret.push_back(s.substr(p, q - p));
    }
    return ret;
}

std::vector<std::wstring> Split(const std::wstring& s, const std::wstring& seps) {
    std::vector<std::wstring> ret;
    for (size_t p = 0, q; p != std::wstring::npos; p = q) {
        p = s.find_first_not_of(seps, p);
        if (p == std::wstring::npos)
            break;
        q = s.find_first_of(seps, p);
        ret.push_back(s.substr(p, q - p));
    }
    return ret;
}

bool EqualsIgnoreCase(const std::string& str1, const std::string& t) {
    return str1.size() == t.size()
           && std::equal(str1.begin(), str1.end(), t.begin(), [](auto a, auto b) {
                  return std::tolower((unsigned char)a) == std::tolower((unsigned char)b);
              });
}

bool EqualsIgnoreCase(const std::wstring& str1, const std::wstring& t) {
    return str1.size() == t.size()
           && std::equal(str1.begin(), str1.end(), t.begin(), [](auto a, auto b) {
                  return std::towlower(a) == std::towlower(b);
              });
}

std::wstring ToLower(const std::wstring& s) {
    std::wstring result = s;
    std::transform(result.begin(), result.end(), result.begin(), towlower);
    return result;
}

// Convert wstring to UTF-8 string
std::string WStrToUTF8(const std::wstring& wstr) {
    if (wstr.empty())
        return {};

    const int sizeNeeded =
        WideCharToMultiByte(CP_UTF8, 0, wstr.c_str(), (int)wstr.length(), nullptr, 0, nullptr, nullptr);
    std::string utf8str(sizeNeeded, 0);
    WideCharToMultiByte(
        CP_UTF8, 0, wstr.c_str(), (int)wstr.length(), &utf8str[0], sizeNeeded, nullptr, nullptr);
    return utf8str;
}

std::string GetWindowTitleExA(HWND hWnd) {
    const int bufferSize = GetWindowTextLengthA(hWnd) + 1;
    std::string windowTitle(bufferSize, '\0');
    if (bufferSize > 1) {
        GetWindowTextA(hWnd, &windowTitle[0], bufferSize);
    }
    return windowTitle;
}

FuzzyMatchResult GetPartialRatioW(const std::wstring& s1, const std::wstring& s2) {
    try {
        return partial_ratio(ToLower(s1), ToLower(s2));
    } catch (...) {
        return { 0.0, 0, -1 };
    }
}

// Helper function to scale a value based on DPI
int ScaleValueForDPI(int value, int dpi) {
    return MulDiv(value, dpi, 96);
}

std::wstring GetApplicationPath() {
    wchar_t applicationPath[MAX_PATH] = {};
    GetModuleFileName(nullptr, applicationPath, MAX_PATH);
    return std::wstring(applicationPath);
}

// Returns true if the current process is running with elevated privileges
bool IsProcessElevated(const bool use_cached_value) {
    static const bool cached_value = []() {
        HANDLE token = nullptr;
        bool elevated = false;
        if (OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token)) {
            TOKEN_ELEVATION elevation = {};
            DWORD size = 0;
            if (GetTokenInformation(token, TokenElevation, &elevation, sizeof(elevation), &size)) {
                elevated = (elevation.TokenIsElevated != 0);
            }
            CloseHandle(token);
        }
        return elevated;
    }();
    return use_cached_value ? cached_value : [&]() {
        HANDLE token = nullptr;
        bool elevated = false;
        if (OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token)) {
            TOKEN_ELEVATION elevation = {};
            DWORD size = 0;
            if (GetTokenInformation(token, TokenElevation, &elevation, sizeof(elevation), &size)) {
                elevated = (elevation.TokenIsElevated != 0);
            }
            CloseHandle(token);
        }
        return elevated;
    }();
}

bool RestartApplication() {
    const std::wstring applicationPath = GetApplicationPath();

    // Replace the current process with a new instance of the same program.
    const intptr_t ret = _wexecl(applicationPath.c_str(), applicationPath.c_str(), nullptr);
    if (ret == -1) {
        const DWORD error = GetLastError();
        (void)error; // only used by logging below
        AT_LOG_ERROR("Failed to restart application. Error code: %d", (int)error);
        return false;
    }
    ExitProcess(0);
}

void RelaunchAsAdminAndExit(const bool elevated, const bool withElvatedArg) {
    AT_LOG_INFO("elevated: %d, withElvatedArg: %d", elevated, withElvatedArg);

    wchar_t exePath[MAX_PATH] = {};
    GetModuleFileNameW(nullptr, exePath, MAX_PATH);

    SHELLEXECUTEINFOW sei = { sizeof(sei) };
    sei.fMask = SEE_MASK_NOCLOSEPROCESS;
    if (elevated) {
        sei.lpVerb = L"runas"; // triggers UAC elevation
    }
    sei.lpFile = exePath;
    sei.lpParameters = withElvatedArg ? L"--elevated" : L"";
    sei.nShow = SW_SHOWNORMAL;

    if (ShellExecuteExW(&sei)) {
        ExitProcess(0);
    }

    const DWORD err = GetLastError();
    if (err == ERROR_CANCELLED) {
        MessageBoxW(nullptr, L"User declined UAC prompt.", L"Info", MB_OK | MB_ICONINFORMATION);
    } else {
        MessageBoxW(nullptr, L"Failed to launch elevated process.", L"Error", MB_OK | MB_ICONERROR);
    }
}

bool InitializeCOM() {
    const HRESULT hr = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    if (FAILED(hr)) {
        return false;
    }

    const HRESULT hrSecurity = CoInitializeSecurity(
        nullptr,
        -1,
        nullptr,
        nullptr,
        RPC_C_AUTHN_LEVEL_DEFAULT,
        RPC_C_IMP_LEVEL_IMPERSONATE,
        nullptr,
        EOAC_NONE,
        nullptr);

    if (FAILED(hrSecurity)) {
        CoUninitialize();
        return false;
    }

    return true;
}

void UninitializeCOM() {
    CoUninitialize();
}

void InitGDIPlus() {
    Gdiplus::GdiplusStartupInput gdiplusStartupInput;
    Gdiplus::GdiplusStartup(&g_gdiplusToken, &gdiplusStartupInput, nullptr);
}

void ShutdownGDIPlus() {
    Gdiplus::GdiplusShutdown(g_gdiplusToken);
}
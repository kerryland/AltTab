#pragma once
#include <vector>
#include <string>
#include "fuzzywuzzy.h"

#define AT_MIN(a, b)    (((a) < (b)) ? (a) : (b))

bool EnableConsoleWindow();

std::string Trim(const std::string& str, const std::string& seps = " \t\r\n");

std::vector<std::string>  Split(const std::string& s,  const std::string& seps = " \t");
std::vector<std::wstring> Split(const std::wstring& s, const std::wstring& seps = L" \t");

bool EqualsIgnoreCase(const std::string& s, const std::string& t);
bool EqualsIgnoreCase(const std::wstring& s, const std::wstring& t);

std::wstring ToLower(const std::wstring& s);

std::string WStrToUTF8(const std::wstring& wstr);

std::string GetWindowTitleExA(HWND hWnd);

FuzzyMatchResult GetPartialRatioW(const std::wstring& s1, const std::wstring& s2);

int ScaleValueForDPI(int value, int dpi = 96);

std::wstring GetApplicationPath();

bool IsProcessElevated(const bool use_cached_value = true);

bool RestartApplication();

void RelaunchAsAdminAndExit(const bool elevated, const bool withElvatedArg);

bool InitializeCOM();

void UninitializeCOM();

void InitGDIPlus();

void ShutdownGDIPlus();
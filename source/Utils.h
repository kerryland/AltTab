#pragma once
#include <vector>
#include <string>
#include <iostream>
#include <string>
#include <locale>
#include <codecvt>

bool EnableConsoleWindow();

std::vector<std::wstring> Split(const std::wstring& s, const std::wstring& seps = L" \t");

bool EqualsIgnoreCase(const std::string& s, const std::string& t);

bool EqualsIgnoreCase(const std::wstring& s, const std::wstring& t);

std::wstring ToLower(const std::wstring& s);

std::wstring ToUpper(const std::wstring& s);

std::string WStrToUTF8(const std::wstring& wstr);

std::wstring UTF8ToWStr(const std::string& utf8str);

std::string GetWindowTitleExA(HWND hWnd);

std::wstring GetWindowTitleExW(HWND hWnd);

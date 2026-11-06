#pragma once

#include <windows.h>
#include <strsafe.h>
#include <cstddef>

inline bool BuildLeagueRegistryLogPath(const wchar_t* tempPath,
                                       wchar_t output[MAX_PATH]) {
    output[0] = L'\0';
    if (tempPath == nullptr || tempPath[0] == L'\0') return false;
    const std::size_t length = wcsnlen_s(tempPath, MAX_PATH);
    if (length == 0 || length >= MAX_PATH) return false;
    if (FAILED(StringCchCopyW(output, MAX_PATH, tempPath))) {
        output[0] = L'\0';
        return false;
    }
    const wchar_t last = output[length - 1];
    if (last != L'\\' && last != L'/') {
        if (FAILED(StringCchCatW(output, MAX_PATH, L"\\"))) {
            output[0] = L'\0';
            return false;
        }
    }
    if (FAILED(StringCchCatW(output, MAX_PATH, L"FIFA12LeagueRegistry.log"))) {
        output[0] = L'\0';
        return false;
    }
    return true;
}


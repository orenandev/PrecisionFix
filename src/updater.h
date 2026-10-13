#pragma once
#include <windows.h>
#include <string>

#ifndef APP_VERSION
#define APP_VERSION "1.0.0"
#endif

struct UpdateInfo {
    std::wstring version;
    std::wstring url;
};

std::wstring AppVersion();
bool CheckForUpdate(UpdateInfo& out);
bool DownloadUpdate(const UpdateInfo& info, std::wstring& newPath, volatile LONG* progress = nullptr);
bool InstallUpdateAndRestart(const std::wstring& newPath);
void CleanupOldVersion();

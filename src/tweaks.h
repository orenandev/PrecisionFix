#pragma once
#include <windows.h>

struct Tweak {
    const wchar_t* name;
    const wchar_t* desc;
    const wchar_t* tech;
    int section;
    bool reboot;
    bool (*isOn)();
    bool (*apply)(bool on);
};

extern Tweak g_tweaks[];
extern const int g_tweakCount;
extern const wchar_t* g_sections[];
extern const int g_sectionCount;

extern Tweak g_quick[];
extern const int g_quickCount;

extern Tweak g_kbd[];
extern const int g_kbdCount;

extern Tweak g_pvp[];
extern const int g_pvpCount;

int FindTweak(const wchar_t* name);
void PurgeMemoryCaches();

void SetRuntimeTimerResolution(bool on);
bool IsTimerTweak(int idx);

DWORD LoadAppDword(const wchar_t* name, DWORD def);
void SaveAppDword(const wchar_t* name, DWORD v);
bool IsElevated();

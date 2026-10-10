#pragma once
#include <windows.h>

struct Tweak {
    const wchar_t* name;
    const wchar_t* desc;   // texto do card
    const wchar_t* tech;   // o que faz de verdade (página Informações)
    int section;
    bool reboot;           // precisa reiniciar para efeito total
    bool (*isOn)();
    bool (*apply)(bool on);
};

// Módulos da página "Precision Fix"
extern Tweak g_tweaks[];
extern const int g_tweakCount;
extern const wchar_t* g_sections[];
extern const int g_sectionCount;

// Ações rápidas do Dashboard ("Central de Otimização")
extern Tweak g_quick[];
extern const int g_quickCount;

// Opções da página "TecladoFix"
extern Tweak g_kbd[];
extern const int g_kbdCount;

// Ajustes de FPS e rede da página "PvP"
extern Tweak g_pvp[];
extern const int g_pvpCount;

int FindTweak(const wchar_t* name);   // índice em g_tweaks pelo nome, -1 se não existir
void PurgeMemoryCaches();             // limpa standby list / modified list

// Mantém a resolução de timer de 0.5ms enquanto o app estiver aberto
void SetRuntimeTimerResolution(bool on);
bool IsTimerTweak(int idx);

// Preferências do app (HKCU\Software\PrecisionFix)
DWORD LoadAppDword(const wchar_t* name, DWORD def);
void SaveAppDword(const wchar_t* name, DWORD v);
bool IsElevated();

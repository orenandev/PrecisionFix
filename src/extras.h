#pragma once
#include <windows.h>
#include <string>
#include <vector>

// ---------------- FiveM ----------------
struct FiveMOpts {
    bool closeApps = false;     // fecha navegadores, Spotify, OneDrive... (Discord fica)
    bool cleanCache = false;    // limpa RAM em standby + cache do FiveM
    bool recommended = false;   // aplica o conjunto recomendado de otimizações
    bool autoBoost = false;     // prioridade alta no processo do jogo ao detectar
};
extern FiveMOpts g_fivem;
void FiveMLoadOpts();
void FiveMSaveOpts();
bool FiveMInstalled();
// Executa as etapas marcadas e abre o FiveM. Roda em thread de trabalho.
bool FiveMPrepareAndLaunch(std::wstring& summary);
void FiveMAutoBoostTick();      // chamar a cada ~3s quando autoBoost estiver ligado

// ---------------- Serviços ----------------
struct ServiceItem { const wchar_t* name; const wchar_t* desc; DWORD defStart; };
enum { SVC_RUNNING, SVC_STOPPED, SVC_DISABLED, SVC_MISSING };
extern ServiceItem g_services[];
extern const int g_serviceCount;
int  ServiceQuery(int i);
bool ServiceSet(int i, bool enable);
bool ServiceWasDisabledByApp(int i);

// ---------------- Display ----------------
struct DisplaySettings { int saturation; int temperature; int gamma100; };  // 50 / 6500 / 100 = neutro
extern DisplaySettings g_disp;
void DisplayLoad();
void DisplaySave();
bool DisplayApply();            // gama/temperatura via gamma ramp, saturação via Magnification
void DisplayShutdown();
bool DisplayIsNeutral();

// ---------------- Ferramentas do FiveM ----------------
enum { TOOL_CACHE, TOOL_LOGS, TOOL_DNS, TOOL_TEMP, TOOL_KILL, TOOL_FOLDER, TOOL_COUNT };
bool FiveMTool(int tool, std::wstring& msg);   // roda em thread de trabalho

// ---------------- Teste de conexão (PvP) ----------------
struct PingTarget { std::wstring name, host; int port; int avg = -1, jitter = -1, loss = -1; };
std::vector<PingTarget> PingTargets();                  // Cloudflare, Google e o servidor salvo
void RunPingTest(std::vector<PingTarget>& targets);     // thread de trabalho (TCP connect, 8 amostras)
std::wstring LoadServerAddr();
void SaveServerAddr(const std::wstring& addr);
bool ParseServerAddr(const std::wstring& in, std::wstring& host, int& port);


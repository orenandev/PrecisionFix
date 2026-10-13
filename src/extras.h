#pragma once
#include <windows.h>
#include <string>
#include <vector>

struct FiveMOpts {
    bool autoBoost = false;
};
extern FiveMOpts g_fivem;
void FiveMLoadOpts();
void FiveMSaveOpts();
void FiveMAutoBoostTick();

struct ServiceItem { const wchar_t* name; const wchar_t* desc; DWORD defStart; };
enum { SVC_RUNNING, SVC_STOPPED, SVC_DISABLED, SVC_MISSING };
extern ServiceItem g_services[];
extern const int g_serviceCount;
int  ServiceQuery(int i);
bool ServiceSet(int i, bool enable);
bool ServiceWasDisabledByApp(int i);

struct DisplaySettings { int saturation; int temperature; int gamma100; };
extern DisplaySettings g_disp;
void DisplayLoad();
void DisplaySave();
bool DisplayApply();
void DisplayShutdown();
bool DisplayIsNeutral();
void DisplayAgentRun();
bool DisplayAgentNotify();
void DisplayAgentQuit();

enum { TOOL_CACHE, TOOL_LOGS, TOOL_DNS, TOOL_TEMP, TOOL_FOLDER, TOOL_COUNT };
bool FiveMTool(int tool, std::wstring& msg);

struct PingTarget { std::wstring name, host; int port; int avg = -1, jitter = -1, loss = -1; };
std::vector<PingTarget> PingTargets();
void RunPingTest(std::vector<PingTarget>& targets);
std::wstring LoadServerAddr();
void SaveServerAddr(const std::wstring& addr);
bool ParseServerAddr(const std::wstring& in, std::wstring& host, int& port);


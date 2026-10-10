#pragma once
#include <string>

struct SysInfo {
    std::wstring cpu, cpuDetail;     // nome / núcleos e threads
    std::wstring gpu;
    std::wstring ramTotal;           // "16 GB"
    std::wstring os, osDetail;       // "Windows 11 Pro" / "24H2 · build 26100"
    std::wstring display;            // "1920x1080 · 144 Hz"
    std::wstring board;
    std::wstring ramDetail;          // "15.9 GB utilizáveis"
    double ramTotalGB = 0;
};

// Lido uma única vez na abertura do app (sem monitoramento contínuo)
extern SysInfo g_sys;
void SysInfoInit();

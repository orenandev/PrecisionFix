#pragma once
#include <string>

struct SysInfo {
    std::wstring cpu, cpuDetail;
    std::wstring gpu;
    std::wstring ramTotal;
    std::wstring os, osDetail;
    std::wstring display;
    std::wstring board;
    std::wstring ramDetail;
    double ramTotalGB = 0;
};

extern SysInfo g_sys;
void SysInfoInit();

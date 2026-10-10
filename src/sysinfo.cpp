#include <windows.h>
#include <vector>
#include <cwchar>
#include "sysinfo.h"

SysInfo g_sys;

static std::wstring RegStr(HKEY root, const wchar_t* path, const wchar_t* name) {
    wchar_t buf[512] = {};
    DWORD sz = sizeof(buf) - sizeof(wchar_t);
    if (RegGetValueW(root, path, name, RRF_RT_REG_SZ, nullptr, buf, &sz) != ERROR_SUCCESS) return L"";
    return buf;
}

static DWORD RegNum(HKEY root, const wchar_t* path, const wchar_t* name) {
    DWORD v = 0, sz = sizeof v;
    RegGetValueW(root, path, name, RRF_RT_REG_DWORD, nullptr, &v, &sz);
    return v;
}

static std::wstring Trim(std::wstring s) {
    size_t a = s.find_first_not_of(L" \t"), b = s.find_last_not_of(L" \t");
    if (a == std::wstring::npos) return L"";
    s = s.substr(a, b - a + 1);
    // colapsa espaços duplos ("Intel(R) Core(TM)   i7")
    std::wstring out;
    for (wchar_t c : s)
        if (!(c == L' ' && !out.empty() && out.back() == L' ')) out += c;
    return out;
}

static void ReplaceAll(std::wstring& s, const std::wstring& a, const std::wstring& b) {
    for (size_t p = 0; (p = s.find(a, p)) != std::wstring::npos; p += b.size()) s.replace(p, a.size(), b);
}


void SysInfoInit() {
    // Processador
    g_sys.cpu = Trim(RegStr(HKEY_LOCAL_MACHINE, L"HARDWARE\\DESCRIPTION\\System\\CentralProcessor\\0", L"ProcessorNameString"));
    ReplaceAll(g_sys.cpu, L"(R)", L"");
    ReplaceAll(g_sys.cpu, L"(TM)", L"");
    ReplaceAll(g_sys.cpu, L" CPU", L"");
    size_t with = g_sys.cpu.find(L" with ");          // "AMD Ryzen 5 5600G with Radeon Graphics"
    if (with != std::wstring::npos) g_sys.cpu.erase(with);
    size_t core = g_sys.cpu.find(L"-Core Processor");  // "AMD Ryzen 7 5800X 8-Core Processor"
    if (core != std::wstring::npos) {
        size_t sp = g_sys.cpu.rfind(L' ', core);
        if (sp != std::wstring::npos) g_sys.cpu.erase(sp);
    }
    g_sys.cpu = Trim(g_sys.cpu);
    if (g_sys.cpu.empty()) g_sys.cpu = L"Desconhecido";

    DWORD len = 0;
    GetLogicalProcessorInformation(nullptr, &len);
    std::vector<SYSTEM_LOGICAL_PROCESSOR_INFORMATION> lpi(len / sizeof(SYSTEM_LOGICAL_PROCESSOR_INFORMATION) + 1);
    int cores = 0, threads = 0;
    if (GetLogicalProcessorInformation(lpi.data(), &len)) {
        for (size_t i = 0; i < len / sizeof(lpi[0]); ++i) {
            if (lpi[i].Relationship != RelationProcessorCore) continue;
            ++cores;
            for (ULONG_PTR m = lpi[i].ProcessorMask; m; m &= m - 1) ++threads;
        }
    }
    DWORD mhz = RegNum(HKEY_LOCAL_MACHINE, L"HARDWARE\\DESCRIPTION\\System\\CentralProcessor\\0", L"~MHz");
    wchar_t tmp[128];
    swprintf(tmp, 128, L"%d núcleos · %d threads · %.2f GHz", cores, threads, mhz / 1000.0);
    g_sys.cpuDetail = tmp;

    // Placa de vídeo (adaptador do monitor principal)
    DISPLAY_DEVICEW dd{};
    dd.cb = sizeof dd;
    for (DWORD i = 0; EnumDisplayDevicesW(nullptr, i, &dd, 0); ++i) {
        if (dd.StateFlags & DISPLAY_DEVICE_PRIMARY_DEVICE) { g_sys.gpu = Trim(dd.DeviceString); break; }
        dd.cb = sizeof dd;
    }
    if (g_sys.gpu.empty()) g_sys.gpu = L"Desconhecida";

    // Sistema operacional
    const wchar_t* cv = L"SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion";
    std::wstring prod = RegStr(HKEY_LOCAL_MACHINE, cv, L"ProductName");
    std::wstring build = RegStr(HKEY_LOCAL_MACHINE, cv, L"CurrentBuildNumber");
    if (_wtoi(build.c_str()) >= 22000) ReplaceAll(prod, L"Windows 10", L"Windows 11");
    g_sys.os = prod.empty() ? L"Windows" : prod;
    std::wstring ver = RegStr(HKEY_LOCAL_MACHINE, cv, L"DisplayVersion");
    g_sys.osDetail = (ver.empty() ? L"" : ver + L" · ") + L"build " + build;

    // Monitor
    DEVMODEW dm{};
    dm.dmSize = sizeof dm;
    if (EnumDisplaySettingsW(nullptr, ENUM_CURRENT_SETTINGS, &dm)) {
        swprintf(tmp, 128, L"%lux%lu · %lu Hz", dm.dmPelsWidth, dm.dmPelsHeight, dm.dmDisplayFrequency);
        g_sys.display = tmp;
    }

    // Placa-mãe
    const wchar_t* bios = L"HARDWARE\\DESCRIPTION\\System\\BIOS";
    g_sys.board = Trim(RegStr(HKEY_LOCAL_MACHINE, bios, L"BaseBoardManufacturer") + L" " +
                       RegStr(HKEY_LOCAL_MACHINE, bios, L"BaseBoardProduct"));
    if (g_sys.board.empty()) g_sys.board = L"Desconhecida";

    // Memória
    MEMORYSTATUSEX ms{};
    ms.dwLength = sizeof ms;
    GlobalMemoryStatusEx(&ms);
    ULONGLONG kb = 0;
    if (GetPhysicallyInstalledSystemMemory(&kb) && kb) g_sys.ramTotalGB = kb / 1048576.0;
    else g_sys.ramTotalGB = ms.ullTotalPhys / 1073741824.0;
    swprintf(tmp, 128, L"%.0f GB", g_sys.ramTotalGB);
    g_sys.ramTotal = tmp;
    swprintf(tmp, 128, L"%.1f GB utilizáveis", ms.ullTotalPhys / 1073741824.0);
    g_sys.ramDetail = tmp;
}

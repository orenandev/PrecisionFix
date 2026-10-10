#include "tweaks.h"
#include <tlhelp32.h>
#include <string>
#include <vector>
#include <cwchar>
#include <cstring>
#include <powrprof.h>

// ============================================================
//  Helpers de registro / processos
// ============================================================

static const wchar_t* kApp = L"Software\\PrecisionFix";

static bool RegSetDword(HKEY root, const wchar_t* path, const wchar_t* name, DWORD v) {
    HKEY k;
    if (RegCreateKeyExW(root, path, 0, nullptr, 0, KEY_SET_VALUE | KEY_WOW64_64KEY, nullptr, &k, nullptr) != ERROR_SUCCESS)
        return false;
    LONG r = RegSetValueExW(k, name, 0, REG_DWORD, (const BYTE*)&v, sizeof v);
    RegCloseKey(k);
    return r == ERROR_SUCCESS;
}

static bool RegGetDword(HKEY root, const wchar_t* path, const wchar_t* name, DWORD& out) {
    HKEY k;
    if (RegOpenKeyExW(root, path, 0, KEY_QUERY_VALUE | KEY_WOW64_64KEY, &k) != ERROR_SUCCESS) return false;
    DWORD type = 0, sz = sizeof out;
    LONG r = RegQueryValueExW(k, name, nullptr, &type, (BYTE*)&out, &sz);
    RegCloseKey(k);
    return r == ERROR_SUCCESS && type == REG_DWORD;
}

static bool RegSetSz(HKEY root, const wchar_t* path, const wchar_t* name, const wchar_t* v) {
    HKEY k;
    if (RegCreateKeyExW(root, path, 0, nullptr, 0, KEY_SET_VALUE | KEY_WOW64_64KEY, nullptr, &k, nullptr) != ERROR_SUCCESS)
        return false;
    LONG r = RegSetValueExW(k, name, 0, REG_SZ, (const BYTE*)v, (DWORD)((wcslen(v) + 1) * sizeof(wchar_t)));
    RegCloseKey(k);
    return r == ERROR_SUCCESS;
}

static std::wstring RegGetSz(HKEY root, const wchar_t* path, const wchar_t* name) {
    HKEY k;
    if (RegOpenKeyExW(root, path, 0, KEY_QUERY_VALUE | KEY_WOW64_64KEY, &k) != ERROR_SUCCESS) return L"";
    wchar_t buf[256] = {};
    DWORD type = 0, sz = sizeof(buf) - sizeof(wchar_t);
    LONG r = RegQueryValueExW(k, name, nullptr, &type, (BYTE*)buf, &sz);
    RegCloseKey(k);
    return (r == ERROR_SUCCESS && type == REG_SZ) ? std::wstring(buf) : L"";
}

static bool RegSetBin(HKEY root, const wchar_t* path, const wchar_t* name, const BYTE* data, DWORD len) {
    HKEY k;
    if (RegCreateKeyExW(root, path, 0, nullptr, 0, KEY_SET_VALUE | KEY_WOW64_64KEY, nullptr, &k, nullptr) != ERROR_SUCCESS)
        return false;
    LONG r = RegSetValueExW(k, name, 0, REG_BINARY, data, len);
    RegCloseKey(k);
    return r == ERROR_SUCCESS;
}

static bool RegBinEquals(HKEY root, const wchar_t* path, const wchar_t* name, const BYTE* data, DWORD len) {
    HKEY k;
    if (RegOpenKeyExW(root, path, 0, KEY_QUERY_VALUE | KEY_WOW64_64KEY, &k) != ERROR_SUCCESS) return false;
    BYTE buf[128];
    DWORD type = 0, sz = sizeof buf;
    LONG r = RegQueryValueExW(k, name, nullptr, &type, buf, &sz);
    RegCloseKey(k);
    return r == ERROR_SUCCESS && sz == len && memcmp(buf, data, len) == 0;
}

static void RegDelValue(HKEY root, const wchar_t* path, const wchar_t* name) {
    HKEY k;
    if (RegOpenKeyExW(root, path, 0, KEY_SET_VALUE | KEY_WOW64_64KEY, &k) != ERROR_SUCCESS) return;
    RegDeleteValueW(k, name);
    RegCloseKey(k);
}

static std::wstring SysExe(const wchar_t* exe) {
    wchar_t dir[MAX_PATH];
    GetSystemDirectoryW(dir, MAX_PATH);
    return L"\"" + std::wstring(dir) + L"\\" + exe + L"\"";
}

static DWORD RunHidden(std::wstring cmd) {
    STARTUPINFOW si{};
    si.cb = sizeof si;
    si.dwFlags = STARTF_USESHOWWINDOW;
    si.wShowWindow = SW_HIDE;
    PROCESS_INFORMATION pi{};
    if (!CreateProcessW(nullptr, &cmd[0], nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi))
        return (DWORD)-1;
    WaitForSingleObject(pi.hProcess, 20000);
    DWORD code = 1;
    GetExitCodeProcess(pi.hProcess, &code);
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
    return code;
}

static bool EnablePrivilege(const wchar_t* priv) {
    HANDLE tok;
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_ADJUST_PRIVILEGES | TOKEN_QUERY, &tok)) return false;
    TOKEN_PRIVILEGES tp{};
    tp.PrivilegeCount = 1;
    tp.Privileges[0].Attributes = SE_PRIVILEGE_ENABLED;
    bool ok = LookupPrivilegeValueW(nullptr, priv, &tp.Privileges[0].Luid) &&
              AdjustTokenPrivileges(tok, FALSE, &tp, sizeof tp, nullptr, nullptr) &&
              GetLastError() == ERROR_SUCCESS;
    CloseHandle(tok);
    return ok;
}

static const UINT SPI_SAVE = SPIF_UPDATEINIFILE | SPIF_SENDCHANGE;

// ============================================================
//  1. Tracking — prioridade de threads de CPU
// ============================================================
static const wchar_t* kPrio = L"SYSTEM\\CurrentControlSet\\Control\\PriorityControl";

static bool TrackingOn() {
    DWORD v = 0;
    return RegGetDword(HKEY_LOCAL_MACHINE, kPrio, L"Win32PrioritySeparation", v) && v == 0x26;
}
static bool TrackingApply(bool on) {
    return RegSetDword(HKEY_LOCAL_MACHINE, kPrio, L"Win32PrioritySeparation", on ? 0x26 : 0x02);
}

// ============================================================
//  2. Sensi — curva MarkC 1:1 (sem aceleração)
// ============================================================
static const wchar_t* kMouse = L"Control Panel\\Mouse";

static const BYTE kMarkCX[40] = {
    0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,  0xC0,0xCC,0x0C,0x00,0x00,0x00,0x00,0x00,
    0x80,0x99,0x19,0x00,0x00,0x00,0x00,0x00,  0x40,0x66,0x26,0x00,0x00,0x00,0x00,0x00,
    0x00,0x33,0x33,0x00,0x00,0x00,0x00,0x00 };
static const BYTE kMarkCY[40] = {
    0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,  0x00,0x00,0x38,0x00,0x00,0x00,0x00,0x00,
    0x00,0x00,0x70,0x00,0x00,0x00,0x00,0x00,  0x00,0x00,0xA8,0x00,0x00,0x00,0x00,0x00,
    0x00,0x00,0xE0,0x00,0x00,0x00,0x00,0x00 };
static const BYTE kDefX[40] = {
    0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,  0x15,0x6E,0x00,0x00,0x00,0x00,0x00,0x00,
    0x00,0x40,0x01,0x00,0x00,0x00,0x00,0x00,  0x29,0xDC,0x03,0x00,0x00,0x00,0x00,0x00,
    0x00,0x00,0x28,0x00,0x00,0x00,0x00,0x00 };
static const BYTE kDefY[40] = {
    0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,  0xFD,0x11,0x01,0x00,0x00,0x00,0x00,0x00,
    0x00,0x24,0x04,0x00,0x00,0x00,0x00,0x00,  0x00,0xFC,0x12,0x00,0x00,0x00,0x00,0x00,
    0x00,0xC0,0xBB,0x01,0x00,0x00,0x00,0x00 };

static bool SensiOn() {
    int m[3] = {};
    SystemParametersInfoW(SPI_GETMOUSE, 0, m, 0);
    return m[2] == 0 && RegBinEquals(HKEY_CURRENT_USER, kMouse, L"SmoothMouseXCurve", kMarkCX, 40);
}
static bool SensiApply(bool on) {
    bool ok = RegSetBin(HKEY_CURRENT_USER, kMouse, L"SmoothMouseXCurve", on ? kMarkCX : kDefX, 40) &&
              RegSetBin(HKEY_CURRENT_USER, kMouse, L"SmoothMouseYCurve", on ? kMarkCY : kDefY, 40);
    int off[3] = {0, 0, 0}, def[3] = {6, 10, 1};
    ok &= SystemParametersInfoW(SPI_SETMOUSE, 0, on ? off : def, SPI_SAVE) != FALSE;
    return ok;
}

// ============================================================
//  3. Flick — hover time + suspensão seletiva USB
// ============================================================
static const wchar_t* kUsbSvc = L"SYSTEM\\CurrentControlSet\\Services\\USB";
static const wchar_t* kUsbSub = L"2a737441-1930-4402-8d77-b2bebba308a3";
static const wchar_t* kUsbSet = L"48e6b7a6-50f5-4782-a5d4-53bb8f07e226";

static bool FlickOn() {
    UINT hover = 400;
    SystemParametersInfoW(SPI_GETMOUSEHOVERTIME, 0, &hover, 0);
    DWORD usb = 0;
    RegGetDword(HKEY_LOCAL_MACHINE, kUsbSvc, L"DisableSelectiveSuspend", usb);
    return hover <= 10 && usb == 1;
}
static bool FlickApply(bool on) {
    bool ok = SystemParametersInfoW(SPI_SETMOUSEHOVERTIME, on ? 8 : 400, nullptr, SPI_SAVE) != FALSE;
    if (on) ok &= RegSetDword(HKEY_LOCAL_MACHINE, kUsbSvc, L"DisableSelectiveSuspend", 1);
    else    RegDelValue(HKEY_LOCAL_MACHINE, kUsbSvc, L"DisableSelectiveSuspend");

    std::wstring pc = SysExe(L"powercfg.exe");
    std::wstring val = on ? L"0" : L"1";
    RunHidden(pc + L" /setacvalueindex scheme_current " + kUsbSub + L" " + kUsbSet + L" " + val);
    RunHidden(pc + L" /setdcvalueindex scheme_current " + kUsbSub + L" " + kUsbSet + L" " + val);
    RunHidden(pc + L" /setactive scheme_current");
    return ok;
}

// ============================================================
//  4. Boost FiveM — prioridade de CPU/IO para GTA/FiveM
// ============================================================
static const wchar_t* kIfeo = L"SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion\\Image File Execution Options\\";
static const wchar_t* kGameExes[] = {
    L"GTA5.exe", L"FiveM.exe", L"FiveM_GTAProcess.exe",
    L"FiveM_b1604_GTAProcess.exe", L"FiveM_b2060_GTAProcess.exe", L"FiveM_b2189_GTAProcess.exe",
    L"FiveM_b2372_GTAProcess.exe", L"FiveM_b2545_GTAProcess.exe", L"FiveM_b2612_GTAProcess.exe",
    L"FiveM_b2699_GTAProcess.exe", L"FiveM_b2802_GTAProcess.exe", L"FiveM_b2944_GTAProcess.exe",
    L"FiveM_b3095_GTAProcess.exe", L"FiveM_b3258_GTAProcess.exe", L"FiveM_b3323_GTAProcess.exe",
    L"FiveM_b3407_GTAProcess.exe", L"FiveM_b3570_GTAProcess.exe",
};

static bool BoostOn() {
    DWORD v = 0;
    std::wstring p = std::wstring(kIfeo) + L"GTA5.exe\\PerfOptions";
    return RegGetDword(HKEY_LOCAL_MACHINE, p.c_str(), L"CpuPriorityClass", v) && v == 3;
}
static bool BoostApply(bool on) {
    bool ok = true;
    for (const wchar_t* exe : kGameExes) {
        std::wstring p = std::wstring(kIfeo) + exe + L"\\PerfOptions";
        if (on) {
            ok &= RegSetDword(HKEY_LOCAL_MACHINE, p.c_str(), L"CpuPriorityClass", 3); // High
            ok &= RegSetDword(HKEY_LOCAL_MACHINE, p.c_str(), L"IoPriority", 3);       // High
        } else {
            RegDeleteTreeW(HKEY_LOCAL_MACHINE, p.c_str());
            RegDeleteKeyExW(HKEY_LOCAL_MACHINE, p.c_str(), KEY_WOW64_64KEY, 0);
        }
    }
    // Aplica também nos processos que já estão rodando
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snap != INVALID_HANDLE_VALUE) {
        PROCESSENTRY32W pe{};
        pe.dwSize = sizeof pe;
        for (BOOL m = Process32FirstW(snap, &pe); m; m = Process32NextW(snap, &pe)) {
            if (_wcsnicmp(pe.szExeFile, L"FiveM", 5) == 0 || _wcsicmp(pe.szExeFile, L"GTA5.exe") == 0) {
                HANDLE h = OpenProcess(PROCESS_SET_INFORMATION, FALSE, pe.th32ProcessID);
                if (h) {
                    SetPriorityClass(h, on ? HIGH_PRIORITY_CLASS : NORMAL_PRIORITY_CLASS);
                    CloseHandle(h);
                }
            }
        }
        CloseHandle(snap);
    }
    return ok;
}

// ============================================================
//  5. 0.5ms — timers de kernel via bcdedit
// ============================================================
static const wchar_t* kKernel = L"SYSTEM\\CurrentControlSet\\Control\\Session Manager\\kernel";

static bool TimerOn() {
    DWORD v = 0;
    return RegGetDword(HKEY_LOCAL_MACHINE, kKernel, L"GlobalTimerResolutionRequests", v) && v == 1;
}
static bool TimerApply(bool on) {
    std::wstring bcd = SysExe(L"bcdedit.exe");
    bool ok = true;
    if (on) {
        ok &= RunHidden(bcd + L" /set disabledynamictick yes") == 0;
        ok &= RunHidden(bcd + L" /set useplatformtick yes") == 0;
        RunHidden(bcd + L" /deletevalue useplatformclock");
        ok &= RegSetDword(HKEY_LOCAL_MACHINE, kKernel, L"GlobalTimerResolutionRequests", 1);
    } else {
        RunHidden(bcd + L" /deletevalue disabledynamictick");
        RunHidden(bcd + L" /deletevalue useplatformtick");
        RegDelValue(HKEY_LOCAL_MACHINE, kKernel, L"GlobalTimerResolutionRequests");
    }
    SetRuntimeTimerResolution(on);
    return ok;
}

typedef LONG (NTAPI* NtSetTimerResolution_t)(ULONG, BOOLEAN, PULONG);

void SetRuntimeTimerResolution(bool on) {
    static bool active = false;
    if (on == active) return;
    auto fn = (NtSetTimerResolution_t)(void*)GetProcAddress(GetModuleHandleW(L"ntdll.dll"), "NtSetTimerResolution");
    if (!fn) return;
    ULONG cur = 0;
    fn(5000, on ? TRUE : FALSE, &cur); // 5000 * 100ns = 0.5ms
    active = on;
}

// ============================================================
//  6. Registros Mouse — MouseDataQueueSize
// ============================================================
static const wchar_t* kMouclass = L"SYSTEM\\CurrentControlSet\\Services\\mouclass\\Parameters";

static bool QueueOn() {
    DWORD v = 0;
    return RegGetDword(HKEY_LOCAL_MACHINE, kMouclass, L"MouseDataQueueSize", v) && v == 20;
}
static bool QueueApply(bool on) {
    return RegSetDword(HKEY_LOCAL_MACHINE, kMouclass, L"MouseDataQueueSize", on ? 20 : 100);
}

// ============================================================
//  7. Islc — sem paginação de drivers + limpa standby list
// ============================================================
static const wchar_t* kMem = L"SYSTEM\\CurrentControlSet\\Control\\Session Manager\\Memory Management";

typedef LONG (NTAPI* NtSetSystemInformation_t)(ULONG, PVOID, ULONG);

void PurgeMemoryCaches() {
    EnablePrivilege(L"SeProfileSingleProcessPrivilege");
    auto fn = (NtSetSystemInformation_t)(void*)GetProcAddress(GetModuleHandleW(L"ntdll.dll"), "NtSetSystemInformation");
    if (!fn) return;
    const ULONG SystemMemoryListInformation = 80;
    ULONG cmd = 3; // MemoryFlushModifiedList
    fn(SystemMemoryListInformation, &cmd, sizeof cmd);
    cmd = 4;       // MemoryPurgeStandbyList
    fn(SystemMemoryListInformation, &cmd, sizeof cmd);
}

static bool IslcOn() {
    DWORD v = 0;
    return RegGetDword(HKEY_LOCAL_MACHINE, kMem, L"DisablePagingExecutive", v) && v == 1;
}
static bool IslcApply(bool on) {
    bool ok = RegSetDword(HKEY_LOCAL_MACHINE, kMem, L"DisablePagingExecutive", on ? 1 : 0);
    if (on) PurgeMemoryCaches();
    return ok;
}

// ============================================================
//  8. Sensi X/Y — velocidade 6/11 (multiplicador 1.0) linear
// ============================================================
static bool SensiXYOn() {
    DWORD flag = 0;
    RegGetDword(HKEY_CURRENT_USER, kApp, L"SensiXY", flag);
    int speed = 0;
    SystemParametersInfoW(SPI_GETMOUSESPEED, 0, &speed, 0);
    return flag == 1 && speed == 10;
}
static bool SensiXYApply(bool on) {
    bool ok = true;
    if (on) {
        int speed = 10, m[3] = {};
        SystemParametersInfoW(SPI_GETMOUSESPEED, 0, &speed, 0);
        SystemParametersInfoW(SPI_GETMOUSE, 0, m, 0);
        RegSetDword(HKEY_CURRENT_USER, kApp, L"PrevMouseSpeed", (DWORD)speed);
        RegSetDword(HKEY_CURRENT_USER, kApp, L"PrevMouse0", (DWORD)m[0]);
        RegSetDword(HKEY_CURRENT_USER, kApp, L"PrevMouse1", (DWORD)m[1]);
        RegSetDword(HKEY_CURRENT_USER, kApp, L"PrevMouse2", (DWORD)m[2]);
        int lin[3] = {0, 0, 0};
        ok &= SystemParametersInfoW(SPI_SETMOUSESPEED, 0, (PVOID)(INT_PTR)10, SPI_SAVE) != FALSE;
        ok &= SystemParametersInfoW(SPI_SETMOUSE, 0, lin, SPI_SAVE) != FALSE;
        ok &= RegSetDword(HKEY_CURRENT_USER, kApp, L"SensiXY", 1);
    } else {
        DWORD speed = 10, a = 6, b = 10, c = 1;
        RegGetDword(HKEY_CURRENT_USER, kApp, L"PrevMouseSpeed", speed);
        RegGetDword(HKEY_CURRENT_USER, kApp, L"PrevMouse0", a);
        RegGetDword(HKEY_CURRENT_USER, kApp, L"PrevMouse1", b);
        RegGetDword(HKEY_CURRENT_USER, kApp, L"PrevMouse2", c);
        if (speed < 1 || speed > 20) speed = 10;
        ok &= SystemParametersInfoW(SPI_SETMOUSESPEED, 0, (PVOID)(INT_PTR)speed, SPI_SAVE) != FALSE;
        if (!SensiOn()) { // não reativa aceleração se o "Sensi" ainda estiver ligado
            int m[3] = {(int)a, (int)b, (int)c};
            SystemParametersInfoW(SPI_SETMOUSE, 0, m, SPI_SAVE);
        }
        RegSetDword(HKEY_CURRENT_USER, kApp, L"SensiXY", 0);
    }
    return ok;
}

// ============================================================
//  9. FilterKeys — remove atraso de repetição do teclado
// ============================================================
static bool FilterOn() {
    FILTERKEYS fk{};
    fk.cbSize = sizeof fk;
    SystemParametersInfoW(SPI_GETFILTERKEYS, sizeof fk, &fk, 0);
    return (fk.dwFlags & FKF_FILTERKEYSON) && fk.iDelayMSec <= 200 && fk.iWaitMSec == 0;
}
static bool FilterApply(bool on) {
    FILTERKEYS fk{};
    fk.cbSize = sizeof fk;
    SystemParametersInfoW(SPI_GETFILTERKEYS, sizeof fk, &fk, 0);
    if (on) {
        fk.dwFlags = (fk.dwFlags | FKF_FILTERKEYSON | FKF_AVAILABLE) & ~(FKF_INDICATOR | FKF_CLICKON);
        fk.iWaitMSec = 0;     // DelayBeforeAcceptance
        fk.iDelayMSec = 200;  // AutoRepeatDelay
        fk.iRepeatMSec = 10;  // AutoRepeatRate
        fk.iBounceMSec = 0;   // BounceTime
    } else {
        fk.dwFlags &= ~FKF_FILTERKEYSON;
        fk.iWaitMSec = 1000;
        fk.iDelayMSec = 1000;
        fk.iRepeatMSec = 500;
        fk.iBounceMSec = 0;
    }
    bool ok = SystemParametersInfoW(SPI_SETFILTERKEYS, sizeof fk, &fk, SPI_SAVE) != FALSE;
    SystemParametersInfoW(SPI_SETKEYBOARDDELAY, on ? 0 : 1, nullptr, SPI_SAVE);
    SystemParametersInfoW(SPI_SETKEYBOARDSPEED, 31, nullptr, SPI_SAVE);
    return ok;
}

// ============================================================
// 10. Flags — prioridade de GPU para jogos (MMCSS)
// ============================================================
static const wchar_t* kGames = L"SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion\\Multimedia\\SystemProfile\\Tasks\\Games";

static bool FlagsOn() {
    DWORD p = 0;
    return RegGetDword(HKEY_LOCAL_MACHINE, kGames, L"Priority", p) && p == 6 &&
           RegGetSz(HKEY_LOCAL_MACHINE, kGames, L"Scheduling Category") == L"High";
}
static bool FlagsApply(bool on) {
    bool ok = RegSetDword(HKEY_LOCAL_MACHINE, kGames, L"GPU Priority", 8);
    ok &= RegSetDword(HKEY_LOCAL_MACHINE, kGames, L"Priority", on ? 6 : 2);
    ok &= RegSetSz(HKEY_LOCAL_MACHINE, kGames, L"Scheduling Category", on ? L"High" : L"Medium");
    ok &= RegSetSz(HKEY_LOCAL_MACHINE, kGames, L"SFIO Priority", on ? L"High" : L"Normal");
    return ok;
}

// ============================================================
// 11. Accessibility — bloqueia StickyKeys e atalhos
// ============================================================
static bool AccessOn() {
    STICKYKEYS sk{}; sk.cbSize = sizeof sk;
    TOGGLEKEYS tk{}; tk.cbSize = sizeof tk;
    FILTERKEYS fk{}; fk.cbSize = sizeof fk;
    SystemParametersInfoW(SPI_GETSTICKYKEYS, sizeof sk, &sk, 0);
    SystemParametersInfoW(SPI_GETTOGGLEKEYS, sizeof tk, &tk, 0);
    SystemParametersInfoW(SPI_GETFILTERKEYS, sizeof fk, &fk, 0);
    return !(sk.dwFlags & SKF_HOTKEYACTIVE) && !(tk.dwFlags & TKF_HOTKEYACTIVE) && !(fk.dwFlags & FKF_HOTKEYACTIVE);
}
static bool AccessApply(bool on) {
    STICKYKEYS sk{}; sk.cbSize = sizeof sk;
    TOGGLEKEYS tk{}; tk.cbSize = sizeof tk;
    FILTERKEYS fk{}; fk.cbSize = sizeof fk;
    SystemParametersInfoW(SPI_GETSTICKYKEYS, sizeof sk, &sk, 0);
    SystemParametersInfoW(SPI_GETTOGGLEKEYS, sizeof tk, &tk, 0);
    SystemParametersInfoW(SPI_GETFILTERKEYS, sizeof fk, &fk, 0);
    if (on) {
        sk.dwFlags &= ~(SKF_HOTKEYACTIVE | SKF_CONFIRMHOTKEY);
        tk.dwFlags &= ~(TKF_HOTKEYACTIVE | TKF_CONFIRMHOTKEY);
        fk.dwFlags &= ~(FKF_HOTKEYACTIVE | FKF_CONFIRMHOTKEY);
    } else {
        sk.dwFlags |= SKF_HOTKEYACTIVE | SKF_CONFIRMHOTKEY;
        tk.dwFlags |= TKF_HOTKEYACTIVE | TKF_CONFIRMHOTKEY;
        fk.dwFlags |= FKF_HOTKEYACTIVE | FKF_CONFIRMHOTKEY;
    }
    bool ok = SystemParametersInfoW(SPI_SETSTICKYKEYS, sizeof sk, &sk, SPI_SAVE) != FALSE;
    ok &= SystemParametersInfoW(SPI_SETTOGGLEKEYS, sizeof tk, &tk, SPI_SAVE) != FALSE;
    ok &= SystemParametersInfoW(SPI_SETFILTERKEYS, sizeof fk, &fk, SPI_SAVE) != FALSE;
    return ok;
}


// ============================================================
//  Ações rápidas do Dashboard
// ============================================================

// Precisão do Mouse (1:1) — remove a aceleração do Windows
static bool MouseOneOn() {
    int m[3] = {};
    SystemParametersInfoW(SPI_GETMOUSE, 0, m, 0);
    return m[2] == 0;
}
static bool MouseOneApply(bool on) {
    int off[3] = {0, 0, 0}, def[3] = {6, 10, 1};
    return SystemParametersInfoW(SPI_SETMOUSE, 0, on ? off : def, SPI_SAVE) != FALSE;
}

// Redução de Latência — Nagle/ACK do TCP + NetworkThrottlingIndex
static const wchar_t* kIfaces = L"SYSTEM\\CurrentControlSet\\Services\\Tcpip\\Parameters\\Interfaces";
static const wchar_t* kMMSys  = L"SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion\\Multimedia\\SystemProfile";

static bool NetOn() {
    DWORD v = 0;
    return RegGetDword(HKEY_LOCAL_MACHINE, kMMSys, L"NetworkThrottlingIndex", v) && v == 0xFFFFFFFF;
}
static bool NetApply(bool on) {
    bool ok = true;
    HKEY k;
    if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, kIfaces, 0, KEY_ENUMERATE_SUB_KEYS | KEY_WOW64_64KEY, &k) == ERROR_SUCCESS) {
        wchar_t sub[256];
        for (DWORD i = 0;; ++i) {
            DWORD len = 256;
            if (RegEnumKeyExW(k, i, sub, &len, nullptr, nullptr, nullptr, nullptr) != ERROR_SUCCESS) break;
            std::wstring p = std::wstring(kIfaces) + L"\\" + sub;
            if (on) {
                ok &= RegSetDword(HKEY_LOCAL_MACHINE, p.c_str(), L"TcpAckFrequency", 1);
                ok &= RegSetDword(HKEY_LOCAL_MACHINE, p.c_str(), L"TCPNoDelay", 1);
                ok &= RegSetDword(HKEY_LOCAL_MACHINE, p.c_str(), L"TcpDelAckTicks", 0);
            } else {
                RegDelValue(HKEY_LOCAL_MACHINE, p.c_str(), L"TcpAckFrequency");
                RegDelValue(HKEY_LOCAL_MACHINE, p.c_str(), L"TCPNoDelay");
                RegDelValue(HKEY_LOCAL_MACHINE, p.c_str(), L"TcpDelAckTicks");
            }
        }
        RegCloseKey(k);
    }
    ok &= RegSetDword(HKEY_LOCAL_MACHINE, kMMSys, L"NetworkThrottlingIndex", on ? 0xFFFFFFFF : 10);
    return ok;
}

// Performance Máxima — plano de energia Alto Desempenho
static const GUID kHighPerf = {0x8c5e7fda, 0xe8bf, 0x4a96, {0x9a, 0x85, 0xa6, 0xe2, 0x3a, 0x8c, 0x63, 0x5c}};
static const GUID kBalanced = {0x381b4222, 0xf694, 0x41f0, {0x96, 0x85, 0xff, 0x5b, 0xb2, 0x60, 0xdf, 0x2e}};
static const GUID kOwnHigh  = {0x50524543, 0x4953, 0x4f4e, {0x46, 0x49, 0x58, 0x00, 0x00, 0x00, 0x00, 0x01}};
static const wchar_t* kOwnHighStr = L"50524543-4953-4f4e-4649-580000000001";

static bool PerfOn() {
    GUID* cur = nullptr;
    if (PowerGetActiveScheme(nullptr, &cur) != ERROR_SUCCESS || !cur) return false;
    bool on = IsEqualGUID(*cur, kHighPerf) || IsEqualGUID(*cur, kOwnHigh);
    LocalFree(cur);
    return on;
}
static bool PerfApply(bool on) {
    if (on) {
        GUID* cur = nullptr;
        if (PowerGetActiveScheme(nullptr, &cur) == ERROR_SUCCESS && cur) {
            if (!IsEqualGUID(*cur, kHighPerf) && !IsEqualGUID(*cur, kOwnHigh))
                RegSetBin(HKEY_CURRENT_USER, kApp, L"PrevPowerScheme", (const BYTE*)cur, sizeof(GUID));
            LocalFree(cur);
        }
        if (PowerSetActiveScheme(nullptr, &kHighPerf) == ERROR_SUCCESS) return true;
        // Notebooks com Modern Standby escondem o plano: cria uma cópia própria dele
        RunHidden(SysExe(L"powercfg.exe") + L" -duplicatescheme 8c5e7fda-e8bf-4a96-9a85-a6e23a8c635c " + kOwnHighStr);
        return PowerSetActiveScheme(nullptr, &kOwnHigh) == ERROR_SUCCESS;
    }
    GUID prev = kBalanced;
    HKEY k;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, kApp, 0, KEY_QUERY_VALUE, &k) == ERROR_SUCCESS) {
        DWORD type = 0, sz = sizeof prev;
        if (RegQueryValueExW(k, L"PrevPowerScheme", nullptr, &type, (BYTE*)&prev, &sz) != ERROR_SUCCESS || sz != sizeof prev)
            prev = kBalanced;
        RegCloseKey(k);
    }
    if (PowerSetActiveScheme(nullptr, &prev) == ERROR_SUCCESS) return true;
    return PowerSetActiveScheme(nullptr, &kBalanced) == ERROR_SUCCESS;
}

// ============================================================
//  Preferências / util
// ============================================================
DWORD LoadAppDword(const wchar_t* name, DWORD def) {
    DWORD v = def;
    return RegGetDword(HKEY_CURRENT_USER, kApp, name, v) ? v : def;
}
void SaveAppDword(const wchar_t* name, DWORD v) { RegSetDword(HKEY_CURRENT_USER, kApp, name, v); }

bool IsElevated() {
    HANDLE tok;
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &tok)) return false;
    TOKEN_ELEVATION e{};
    DWORD sz = 0;
    bool r = GetTokenInformation(tok, TokenElevation, &e, sizeof e, &sz) && e.TokenIsElevated;
    CloseHandle(tok);
    return r;
}

// ============================================================
//  Tabelas
// ============================================================
const wchar_t* g_sections[] = {
    L"MOTOR DE MOVIMENTO",
    L"LATÊNCIA DO SISTEMA",
    L"TECLADO & ENTRADA",
};
const int g_sectionCount = sizeof(g_sections) / sizeof(g_sections[0]);

Tweak g_tweaks[] = {
    { L"Tracking",        L"Otimiza o fluxo de dados do sensor para movimentos mais fluidos.",
      L"Altera prioridade de threads de CPU",                0, false, TrackingOn, TrackingApply },
    { L"Sensi",           L"Normaliza a curva de sensibilidade em todos os níveis de DPI.",
      L"Aplica curva MarkC 1:1 física no registro",          0, true,  SensiOn,    SensiApply    },
    { L"Flick",           L"Melhora a resposta durante mudanças rápidas de direção.",
      L"Ajusta tempo de hover e desativa suspensão USB",     0, false, FlickOn,    FlickApply    },
    { L"Boost Fivem",     L"Prioridade de CPU e otimização para GTA/FiveM.",
      L"Prioridade de CPU e otimização para GTA/FiveM",      0, false, BoostOn,    BoostApply    },
    { L"0.5ms",           L"Ajusta os timers do kernel para menor latência de entrada.",
      L"Ajusta timers de Kernel via bcdedit",                1, true,  TimerOn,    TimerApply    },
    { L"Registros Mouse", L"Reduz a fila de dados do mouse para resposta mais imediata.",
      L"Ajusta tamanho da fila MouseDataQueueSize",          1, true,  QueueOn,    QueueApply    },
    { L"Islc",            L"Previne paginação de drivers na RAM e limpa os caches de memória.",
      L"Previne paginação de drivers na RAM e limpa caches", 1, true,  IslcOn,     IslcApply     },
    { L"Flags",           L"Ajusta a prioridade da GPU para processos de jogos.",
      L"Ajusta prioridade da GPU para processos de jogos",   1, false, FlagsOn,    FlagsApply    },
    { L"Sensi X/Y",       L"Uniformiza a sensibilidade horizontal e vertical em 1:1.",
      L"Uniformiza sensibilidade horizontal e vertical",     2, false, SensiXYOn,  SensiXYApply  },
    { L"Accessibility",   L"Bloqueia StickyKeys e atalhos inconvenientes durante o jogo.",
      L"Bloqueia StickyKeys e atalhos inconvenientes",       2, false, AccessOn,   AccessApply   },
};
const int g_tweakCount = sizeof(g_tweaks) / sizeof(g_tweaks[0]);

Tweak g_quick[] = {
    { L"Precisão do Mouse (1:1)", L"Remove a aceleração do Windows para mira perfeita.",
      L"Desativa \"Aumentar precisão do ponteiro\"",          -1, false, MouseOneOn, MouseOneApply },
    { L"Redução de Latência",     L"Otimiza os pacotes TCP para diminuir o PING em jogos.",
      L"TcpAckFrequency, TCPNoDelay, NetworkThrottlingIndex", -1, true,  NetOn,      NetApply      },
    { L"Performance Máxima",      L"Ativa o plano de energia de alto desempenho.",
      L"Plano de energia Alto Desempenho",                    -1, false, PerfOn,     PerfApply     },
};
const int g_quickCount = sizeof(g_quick) / sizeof(g_quick[0]);

bool IsTimerTweak(int idx) { return idx >= 0 && idx < g_tweakCount && g_tweaks[idx].apply == TimerApply; }

// ============================================================
//  TecladoFix
// ============================================================
static const wchar_t* kKbdclass = L"SYSTEM\\CurrentControlSet\\Services\\kbdclass\\Parameters";

// Perfil de Precisão — fila de dados do teclado menor (resposta mais imediata)
static bool KbdQueueOn() {
    DWORD v = 0;
    return RegGetDword(HKEY_LOCAL_MACHINE, kKbdclass, L"KeyboardDataQueueSize", v) && v == 20;
}
static bool KbdQueueApply(bool on) {
    return RegSetDword(HKEY_LOCAL_MACHINE, kKbdclass, L"KeyboardDataQueueSize", on ? 20 : 100);
}

// Desativar Filter Keys — desliga o recurso e o atalho (segurar Shift direito 8s)
static bool NoFilterOn() {
    FILTERKEYS fk{};
    fk.cbSize = sizeof fk;
    SystemParametersInfoW(SPI_GETFILTERKEYS, sizeof fk, &fk, 0);
    return !(fk.dwFlags & FKF_FILTERKEYSON) && !(fk.dwFlags & FKF_HOTKEYACTIVE);
}
static bool NoFilterApply(bool on) {
    FILTERKEYS fk{};
    fk.cbSize = sizeof fk;
    SystemParametersInfoW(SPI_GETFILTERKEYS, sizeof fk, &fk, 0);
    if (on) fk.dwFlags &= ~(FKF_FILTERKEYSON | FKF_HOTKEYACTIVE | FKF_CONFIRMHOTKEY);
    else    fk.dwFlags |= FKF_HOTKEYACTIVE | FKF_CONFIRMHOTKEY;
    return SystemParametersInfoW(SPI_SETFILTERKEYS, sizeof fk, &fk, SPI_SAVE) != FALSE;
}

// Atraso de Repetição Mínimo — KeyboardDelay 0 (padrão 1)
static bool MinDelayOn() {
    int d = 1;
    SystemParametersInfoW(SPI_GETKEYBOARDDELAY, 0, &d, 0);
    return d == 0;
}
static bool MinDelayApply(bool on) {
    return SystemParametersInfoW(SPI_SETKEYBOARDDELAY, on ? 0 : 1, nullptr, SPI_SAVE) != FALSE;
}

// Taxa de Repetição Máxima — KeyboardSpeed 31
static bool MaxRateOn() {
    DWORD s = 0;
    SystemParametersInfoW(SPI_GETKEYBOARDSPEED, 0, &s, 0);
    return s == 31;
}
static bool MaxRateApply(bool on) {
    if (on) {
        DWORD s = 31;
        SystemParametersInfoW(SPI_GETKEYBOARDSPEED, 0, &s, 0);
        if (s != 31) RegSetDword(HKEY_CURRENT_USER, kApp, L"PrevKeyboardSpeed", s);
        return SystemParametersInfoW(SPI_SETKEYBOARDSPEED, 31, nullptr, SPI_SAVE) != FALSE;
    }
    DWORD prev = 20;
    RegGetDword(HKEY_CURRENT_USER, kApp, L"PrevKeyboardSpeed", prev);
    if (prev >= 31) prev = 20;
    return SystemParametersInfoW(SPI_SETKEYBOARDSPEED, prev, nullptr, SPI_SAVE) != FALSE;
}

// Prioridade de Processo em Jogo — MMCSS SystemResponsiveness 0 (padrão 20)
static bool GamePrioOn() {
    DWORD v = 20;
    return RegGetDword(HKEY_LOCAL_MACHINE, kMMSys, L"SystemResponsiveness", v) && v == 0;
}
static bool GamePrioApply(bool on) {
    return RegSetDword(HKEY_LOCAL_MACHINE, kMMSys, L"SystemResponsiveness", on ? 0 : 20);
}

// Aplicar Ajustes de Registro — prioridade máxima da thread do driver de teclado
static bool KbdRegOn() {
    DWORD v = 0;
    return RegGetDword(HKEY_LOCAL_MACHINE, kKbdclass, L"ThreadPriority", v) && v == 31;
}
static bool KbdRegApply(bool on) {
    if (on) return RegSetDword(HKEY_LOCAL_MACHINE, kKbdclass, L"ThreadPriority", 31);
    RegDelValue(HKEY_LOCAL_MACHINE, kKbdclass, L"ThreadPriority");
    return true;
}

// Suspensão USB Seletiva Off
static bool UsbOn() {
    DWORD v = 0;
    return RegGetDword(HKEY_LOCAL_MACHINE, kUsbSvc, L"DisableSelectiveSuspend", v) && v == 1;
}
static bool UsbApply(bool on) {
    bool ok = true;
    if (on) ok = RegSetDword(HKEY_LOCAL_MACHINE, kUsbSvc, L"DisableSelectiveSuspend", 1);
    else    RegDelValue(HKEY_LOCAL_MACHINE, kUsbSvc, L"DisableSelectiveSuspend");
    std::wstring pc = SysExe(L"powercfg.exe");
    std::wstring val = on ? L"0" : L"1";
    RunHidden(pc + L" /setacvalueindex scheme_current " + kUsbSub + L" " + kUsbSet + L" " + val);
    RunHidden(pc + L" /setdcvalueindex scheme_current " + kUsbSub + L" " + kUsbSet + L" " + val);
    RunHidden(pc + L" /setactive scheme_current");
    return ok;
}


// ============================================================
//  PvP — ajustes de FPS e rede
// ============================================================

// Game DVR / captura do Xbox Game Bar (grava em segundo plano e custa FPS)
static const wchar_t* kGameCfg = L"System\\GameConfigStore";
static const wchar_t* kGameDvr = L"Software\\Microsoft\\Windows\\CurrentVersion\\GameDVR";

static bool DvrOn() {
    DWORD v = 1;
    return RegGetDword(HKEY_CURRENT_USER, kGameCfg, L"GameDVR_Enabled", v) && v == 0;
}
static bool DvrApply(bool on) {
    bool ok = RegSetDword(HKEY_CURRENT_USER, kGameCfg, L"GameDVR_Enabled", on ? 0 : 1);
    if (on) ok &= RegSetDword(HKEY_CURRENT_USER, kGameDvr, L"AppCaptureEnabled", 0);
    else    RegDelValue(HKEY_CURRENT_USER, kGameDvr, L"AppCaptureEnabled");
    return ok;
}

// Modo de Jogo do Windows
static const wchar_t* kGameBar = L"Software\\Microsoft\\GameBar";
static bool GameModeOn() {
    DWORD v = 1;
    RegGetDword(HKEY_CURRENT_USER, kGameBar, L"AutoGameModeEnabled", v);  // ausente = ligado (padrão)
    return v == 1;
}
static bool GameModeApply(bool on) {
    if (on) return RegSetDword(HKEY_CURRENT_USER, kGameBar, L"AutoGameModeEnabled", 1);
    return RegSetDword(HKEY_CURRENT_USER, kGameBar, L"AutoGameModeEnabled", 0);
}

// Agendamento de GPU acelerado por hardware (HAGS)
static const wchar_t* kGfx = L"SYSTEM\\CurrentControlSet\\Control\\GraphicsDrivers";
static bool HagsOn() {
    DWORD v = 0;
    return RegGetDword(HKEY_LOCAL_MACHINE, kGfx, L"HwSchMode", v) && v == 2;
}
static bool HagsApply(bool on) { return RegSetDword(HKEY_LOCAL_MACHINE, kGfx, L"HwSchMode", on ? 2 : 1); }

// QoS: marca os pacotes do FiveM com DSCP 46 (prioridade em roteadores com QoS)
static const wchar_t* kQos = L"SOFTWARE\\Policies\\Microsoft\\Windows\\QoS\\";
static const wchar_t* kQosNla = L"SYSTEM\\CurrentControlSet\\Services\\Tcpip\\QoS";

static bool QosOn() {
    HKEY k;
    std::wstring p = std::wstring(kQos) + L"PrecisionFix GTA5.exe";
    if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, p.c_str(), 0, KEY_QUERY_VALUE | KEY_WOW64_64KEY, &k) != ERROR_SUCCESS) return false;
    RegCloseKey(k);
    return true;
}
static bool QosApply(bool on) {
    bool ok = true;
    for (const wchar_t* exe : kGameExes) {
        std::wstring p = std::wstring(kQos) + L"PrecisionFix " + exe;
        if (on) {
            ok &= RegSetSz(HKEY_LOCAL_MACHINE, p.c_str(), L"Version", L"1.0");
            ok &= RegSetSz(HKEY_LOCAL_MACHINE, p.c_str(), L"Application Name", exe);
            const wchar_t* any[] = {L"Protocol", L"Local Port", L"Local IP", L"Local IP Prefix Length",
                                    L"Remote Port", L"Remote IP", L"Remote IP Prefix Length"};
            for (const wchar_t* n : any) ok &= RegSetSz(HKEY_LOCAL_MACHINE, p.c_str(), n, L"*");
            ok &= RegSetSz(HKEY_LOCAL_MACHINE, p.c_str(), L"DSCP Value", L"46");
            ok &= RegSetSz(HKEY_LOCAL_MACHINE, p.c_str(), L"Throttle Rate", L"-1");
        } else {
            RegDeleteTreeW(HKEY_LOCAL_MACHINE, p.c_str());
            RegDeleteKeyExW(HKEY_LOCAL_MACHINE, p.c_str(), KEY_WOW64_64KEY, 0);
        }
    }
    // Em PCs fora de domínio o Windows só aplica QoS com esta opção
    if (on) RegSetSz(HKEY_LOCAL_MACHINE, kQosNla, L"Do not use NLA", L"1");
    return ok;
}

Tweak g_pvp[] = {
    { L"Game DVR Off",           L"Desliga a gravação em segundo plano do Xbox Game Bar, que custa FPS.",
      L"GameDVR_Enabled = 0, AppCaptureEnabled = 0",       -1, false, DvrOn,      DvrApply      },
    { L"Modo de Jogo",           L"Windows prioriza o jogo e segura atualizações durante a partida.",
      L"AutoGameModeEnabled = 1",                          -1, false, GameModeOn, GameModeApply },
    { L"Agendamento de GPU (HAGS)", L"A placa de vídeo gerencia a própria memória: menos latência de frame.",
      L"HwSchMode = 2",                                    -1, true,  HagsOn,     HagsApply     },
    { L"Prioridade de pacotes (QoS)", L"Marca o tráfego do FiveM com DSCP 46 para roteadores com QoS.",
      L"Política QoS DSCP 46 para FiveM/GTA5",             -1, true,  QosOn,      QosApply      },
};
const int g_pvpCount = sizeof(g_pvp) / sizeof(g_pvp[0]);
// Ordem: 4 da coluna esquerda, depois 4 da direita
Tweak g_kbd[] = {
    { L"Perfil de Precisão",          L"Reduz a fila de dados do teclado para resposta imediata.",
      L"KeyboardDataQueueSize = 20",                   -1, true,  KbdQueueOn, KbdQueueApply },
    { L"Desativar Filter Keys",       L"Desliga o Filter Keys e o atalho do Shift direito.",
      L"Desliga FilterKeys e o atalho de ativação",    -1, false, NoFilterOn, NoFilterApply },
    { L"Atraso de Repetição Mínimo",  L"Tecla começa a repetir quase sem espera.",
      L"KeyboardDelay = 0",                            -1, false, MinDelayOn, MinDelayApply },
    { L"Taxa de Repetição Máxima",    L"Repetição de teclas na velocidade máxima.",
      L"KeyboardSpeed = 31",                           -1, false, MaxRateOn,  MaxRateApply  },
    { L"Prioridade de Processo em Jogo", L"Reserva menos CPU para tarefas de fundo durante o jogo.",
      L"SystemResponsiveness = 0",                     -1, false, GamePrioOn, GamePrioApply },
    { L"Aplicar Ajustes de Registro", L"Prioridade máxima para o driver do teclado.",
      L"kbdclass ThreadPriority = 31",                 -1, true,  KbdRegOn,   KbdRegApply   },
    { L"Suspensão USB Seletiva Off",  L"Evita que o Windows desligue portas USB para economizar energia.",
      L"DisableSelectiveSuspend + powercfg",           -1, false, UsbOn,      UsbApply      },
    { L"Desativar Sticky/Toggle Key", L"Bloqueia os atalhos de StickyKeys e ToggleKeys.",
      L"Remove os atalhos de StickyKeys/ToggleKeys",   -1, false, AccessOn,   AccessApply   },
};
const int g_kbdCount = sizeof(g_kbd) / sizeof(g_kbd[0]);

int FindTweak(const wchar_t* name) {
    for (int i = 0; i < g_tweakCount; ++i)
        if (wcscmp(g_tweaks[i].name, name) == 0) return i;
    return -1;
}

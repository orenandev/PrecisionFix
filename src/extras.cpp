#include "extras.h"
#include "tweaks.h"
#include <tlhelp32.h>
#include <shellapi.h>
#include <shlobj.h>
#include <vector>
#include <cmath>
#include <cwchar>
#include <algorithm>
#include <objidl.h>
#include <gdiplus.h>

static const wchar_t* kApp = L"Software\\PrecisionFix";

static bool FileExists(const std::wstring& p) {
    DWORD a = GetFileAttributesW(p.c_str());
    return a != INVALID_FILE_ATTRIBUTES && !(a & FILE_ATTRIBUTE_DIRECTORY);
}
static bool DirExists(const std::wstring& p) {
    DWORD a = GetFileAttributesW(p.c_str());
    return a != INVALID_FILE_ATTRIBUTES && (a & FILE_ATTRIBUTE_DIRECTORY);
}
static std::wstring LocalAppData() {
    wchar_t buf[MAX_PATH] = {};
    SHGetFolderPathW(nullptr, CSIDL_LOCAL_APPDATA, nullptr, 0, buf);
    return buf;
}

// ============================================================
//  FiveM
// ============================================================
FiveMOpts g_fivem;

void FiveMLoadOpts() {
    g_fivem.closeApps   = LoadAppDword(L"FiveM_CloseApps", 0) != 0;
    g_fivem.cleanCache  = LoadAppDword(L"FiveM_CleanCache", 0) != 0;
    g_fivem.recommended = LoadAppDword(L"FiveM_Recommended", 0) != 0;
    g_fivem.autoBoost   = LoadAppDword(L"FiveM_AutoBoost", 0) != 0;
}
void FiveMSaveOpts() {
    SaveAppDword(L"FiveM_CloseApps", g_fivem.closeApps);
    SaveAppDword(L"FiveM_CleanCache", g_fivem.cleanCache);
    SaveAppDword(L"FiveM_Recommended", g_fivem.recommended);
    SaveAppDword(L"FiveM_AutoBoost", g_fivem.autoBoost);
}

static std::wstring FiveMExe() { return LocalAppData() + L"\\FiveM\\FiveM.exe"; }
bool FiveMInstalled() { return FileExists(FiveMExe()); }

// Apps de segundo plano que pesam durante o jogo (Discord e Steam ficam abertos)
static const wchar_t* kBackgroundApps[] = {
    L"chrome.exe", L"msedge.exe", L"firefox.exe", L"opera.exe", L"brave.exe",
    L"Spotify.exe", L"OneDrive.exe", L"Teams.exe", L"ms-teams.exe", L"EpicGamesLauncher.exe",
};

static int CloseBackgroundApps() {
    int closed = 0;
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snap == INVALID_HANDLE_VALUE) return 0;
    PROCESSENTRY32W pe{};
    pe.dwSize = sizeof pe;
    for (BOOL m = Process32FirstW(snap, &pe); m; m = Process32NextW(snap, &pe)) {
        for (const wchar_t* app : kBackgroundApps) {
            if (_wcsicmp(pe.szExeFile, app) != 0) continue;
            HANDLE h = OpenProcess(PROCESS_TERMINATE, FALSE, pe.th32ProcessID);
            if (h) {
                if (TerminateProcess(h, 0)) ++closed;
                CloseHandle(h);
            }
            break;
        }
    }
    CloseHandle(snap);
    return closed;
}

static void DeleteTree(const std::wstring& path) {
    if (!DirExists(path) && !FileExists(path)) return;
    std::wstring from = path;
    from.push_back(L'\0');  // SHFileOperation exige lista terminada em \0\0
    SHFILEOPSTRUCTW op{};
    op.wFunc = FO_DELETE;
    op.pFrom = from.c_str();
    op.fFlags = FOF_NO_UI;
    SHFileOperationW(&op);
}

static ULONGLONG DirSize(const std::wstring& dir) {
    ULONGLONG total = 0;
    WIN32_FIND_DATAW fd;
    HANDLE f = FindFirstFileW((dir + L"\\*").c_str(), &fd);
    if (f == INVALID_HANDLE_VALUE) return 0;
    do {
        std::wstring n = fd.cFileName;
        if (n == L"." || n == L"..") continue;
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) continue;   // não segue links
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) total += DirSize(dir + L"\\" + n);
        else total += ((ULONGLONG)fd.nFileSizeHigh << 32) | fd.nFileSizeLow;
    } while (FindNextFileW(f, &fd));
    FindClose(f);
    return total;
}

// Pasta do FiveM.app (o Explorer mostra como "FiveM Application Data")
static std::wstring FiveMAppDir() {
    std::wstring base = LocalAppData() + L"\\FiveM\\";
    if (DirExists(base + L"FiveM.app")) return base + L"FiveM.app";
    if (DirExists(base + L"FiveM Application Data")) return base + L"FiveM Application Data";
    return L"";
}

// Cache do FiveM: apaga tudo dentro de data\ MENOS game-storage (arquivos do jogo, vários GB).
// Em instalações antigas os arquivos do jogo ficavam em data\cache\game — esse também é mantido.
static bool CleanFiveMCache(ULONGLONG* freed = nullptr) {
    std::wstring app = FiveMAppDir();
    if (app.empty()) return false;
    std::wstring data = app + L"\\data";
    if (!DirExists(data)) return false;
    ULONGLONG before = DirSize(data);
    WIN32_FIND_DATAW fd;
    HANDLE f = FindFirstFileW((data + L"\\*").c_str(), &fd);
    if (f != INVALID_HANDLE_VALUE) {
        do {
            std::wstring n = fd.cFileName;
            if (n == L"." || n == L".." || _wcsicmp(n.c_str(), L"game-storage") == 0) continue;
            std::wstring p = data + L"\\" + n;
            if (_wcsicmp(n.c_str(), L"cache") == 0 && DirExists(p + L"\\game")) {
                WIN32_FIND_DATAW cd;   // cache legado: apaga tudo menos cache\game
                HANDLE c = FindFirstFileW((p + L"\\*").c_str(), &cd);
                if (c != INVALID_HANDLE_VALUE) {
                    do {
                        std::wstring cn = cd.cFileName;
                        if (cn == L"." || cn == L".." || _wcsicmp(cn.c_str(), L"game") == 0) continue;
                        DeleteTree(p + L"\\" + cn);
                    } while (FindNextFileW(c, &cd));
                    FindClose(c);
                }
                continue;
            }
            DeleteTree(p);
        } while (FindNextFileW(f, &fd));
        FindClose(f);
    }
    if (freed) {
        ULONGLONG after = DirSize(data);
        *freed = before > after ? before - after : 0;
    }
    return true;
}

// ---------------- Ferramentas do FiveM ----------------
static std::wstring FormatSize(ULONGLONG b) {
    wchar_t buf[64];
    if (b >= 1073741824ULL) swprintf(buf, 64, L"%.2f GB", b / 1073741824.0);
    else swprintf(buf, 64, L"%.1f MB", b / 1048576.0);
    return buf;
}

static bool IsFiveMProcess(const wchar_t* exe) {
    return _wcsnicmp(exe, L"FiveM", 5) == 0 || _wcsicmp(exe, L"GTA5.exe") == 0;
}

static ULONGLONG CleanDirContents(const std::wstring& dir) {
    if (!DirExists(dir)) return 0;
    ULONGLONG before = DirSize(dir);
    WIN32_FIND_DATAW fd;
    HANDLE f = FindFirstFileW((dir + L"\\*").c_str(), &fd);
    if (f != INVALID_HANDLE_VALUE) {
        do {
            std::wstring n = fd.cFileName;
            if (n == L"." || n == L"..") continue;
            std::wstring p = dir + L"\\" + n;
            if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) DeleteTree(p);
            else DeleteFileW(p.c_str());   // arquivos em uso são ignorados
        } while (FindNextFileW(f, &fd));
        FindClose(f);
    }
    ULONGLONG after = DirSize(dir);
    return before > after ? before - after : 0;
}

bool FiveMTool(int tool, std::wstring& msg) {
    switch (tool) {
    case TOOL_CACHE: {
        ULONGLONG freed = 0;
        if (!CleanFiveMCache(&freed)) { msg = L"Pasta do FiveM não encontrada"; return false; }
        msg = L"Cache do FiveM limpo · " + FormatSize(freed) + L" liberados (game-storage mantida)";
        return true;
    }
    case TOOL_LOGS: {
        std::wstring app = FiveMAppDir();
        if (app.empty()) { msg = L"Pasta do FiveM não encontrada"; return false; }
        ULONGLONG freed = CleanDirContents(app + L"\\logs") + CleanDirContents(app + L"\\crashes");
        msg = L"Logs e crashes apagados · " + FormatSize(freed) + L" liberados";
        return true;
    }
    case TOOL_DNS: {
        typedef BOOL (WINAPI* Flush_t)();
        HMODULE dns = LoadLibraryW(L"dnsapi.dll");
        auto fn = dns ? (Flush_t)(void*)GetProcAddress(dns, "DnsFlushResolverCache") : nullptr;
        bool ok = fn && fn();
        msg = ok ? L"Cache de DNS limpo" : L"Não foi possível limpar o DNS";
        return ok;
    }
    case TOOL_TEMP: {
        wchar_t tmp[MAX_PATH];
        GetTempPathW(MAX_PATH, tmp);
        std::wstring t = tmp;
        if (!t.empty() && t.back() == L'\\') t.pop_back();
        ULONGLONG freed = CleanDirContents(t);
        msg = L"Arquivos temporários limpos · " + FormatSize(freed) + L" liberados";
        return true;
    }
    case TOOL_KILL: {
        int n = 0;
        HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
        if (snap != INVALID_HANDLE_VALUE) {
            PROCESSENTRY32W pe{};
            pe.dwSize = sizeof pe;
            for (BOOL m = Process32FirstW(snap, &pe); m; m = Process32NextW(snap, &pe)) {
                if (!IsFiveMProcess(pe.szExeFile)) continue;
                HANDLE h = OpenProcess(PROCESS_TERMINATE, FALSE, pe.th32ProcessID);
                if (h) { if (TerminateProcess(h, 0)) ++n; CloseHandle(h); }
            }
            CloseHandle(snap);
        }
        msg = n ? L"FiveM encerrado" : L"O FiveM não estava aberto";
        return true;
    }
    case TOOL_FOLDER: {
        std::wstring app = FiveMAppDir();
        if (app.empty()) { msg = L"Pasta do FiveM não encontrada"; return false; }
        ShellExecuteW(nullptr, L"open", L"explorer.exe", (L"\"" + app + L"\"").c_str(), nullptr, SW_SHOWNORMAL);
        msg = L"Pasta do FiveM aberta";
        return true;
    }
    }
    return false;
}

static void ApplyRecommended() {
    const wchar_t* names[] = {L"Tracking", L"Flags", L"Boost Fivem"};
    for (const wchar_t* n : names) {
        int i = FindTweak(n);
        if (i >= 0 && !g_tweaks[i].isOn()) g_tweaks[i].apply(true);
    }
    for (int i = 0; i < g_quickCount; ++i)   // Performance Máxima (plano de energia)
        if (wcscmp(g_quick[i].name, L"Performance Máxima") == 0 && !g_quick[i].isOn()) g_quick[i].apply(true);
}

bool FiveMPrepareAndLaunch(std::wstring& summary) {
    summary.clear();
    if (g_fivem.closeApps) {
        int n = CloseBackgroundApps();
        wchar_t b[64];
        swprintf(b, 64, L"%d apps fechados", n);
        summary += b;
    }
    if (g_fivem.cleanCache) {
        PurgeMemoryCaches();
        bool c = CleanFiveMCache();
        if (!summary.empty()) summary += L" · ";
        summary += c ? L"RAM e cache limpos" : L"RAM limpa";
    }
    if (g_fivem.recommended) {
        ApplyRecommended();
        if (!summary.empty()) summary += L" · ";
        summary += L"otimizações aplicadas";
    }
    // Abre via explorer.exe para o FiveM NÃO herdar o modo administrador (o FiveM recusa rodar como admin)
    std::wstring exe = FiveMExe();
    HINSTANCE r;
    if (FileExists(exe)) r = ShellExecuteW(nullptr, L"open", L"explorer.exe", exe.c_str(), nullptr, SW_SHOWNORMAL);
    else                 r = ShellExecuteW(nullptr, L"open", L"explorer.exe", L"fivem://", nullptr, SW_SHOWNORMAL);
    return (INT_PTR)r > 32;
}

void FiveMAutoBoostTick() {
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snap == INVALID_HANDLE_VALUE) return;
    PROCESSENTRY32W pe{};
    pe.dwSize = sizeof pe;
    for (BOOL m = Process32FirstW(snap, &pe); m; m = Process32NextW(snap, &pe)) {
        bool game = (_wcsnicmp(pe.szExeFile, L"FiveM_", 6) == 0 && wcsstr(pe.szExeFile, L"GTAProcess")) ||
                    _wcsicmp(pe.szExeFile, L"GTA5.exe") == 0;
        if (!game) continue;
        HANDLE h = OpenProcess(PROCESS_SET_INFORMATION | PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pe.th32ProcessID);
        if (h) {
            if (GetPriorityClass(h) != HIGH_PRIORITY_CLASS) SetPriorityClass(h, HIGH_PRIORITY_CLASS);
            CloseHandle(h);
        }
    }
    CloseHandle(snap);
}

// ============================================================
//  Serviços
// ============================================================
ServiceItem g_services[] = {
    {L"DiagTrack",    L"Telemetria: envia dados de uso para a Microsoft",          SERVICE_AUTO_START},
    {L"DPS",          L"Política de diagnóstico: detecta e soluciona problemas",   SERVICE_AUTO_START},
    {L"PcaSvc",       L"Assistente de compatibilidade de programas",              SERVICE_AUTO_START},
    {L"SysMain",      L"Superfetch: pré-carrega apps na RAM e usa o disco",       SERVICE_AUTO_START},
    {L"SsdpSrv",      L"Descoberta de dispositivos UPnP/SSDP na rede",            SERVICE_DEMAND_START},
    {L"UmRdpService", L"Redirecionador de portas da Área de Trabalho Remota",     SERVICE_DEMAND_START},
};
const int g_serviceCount = sizeof(g_services) / sizeof(g_services[0]);

static std::wstring SvcKey(int i) { return std::wstring(L"SvcStart_") + g_services[i].name; }

int ServiceQuery(int i) {
    SC_HANDLE scm = OpenSCManagerW(nullptr, nullptr, SC_MANAGER_CONNECT);
    if (!scm) return SVC_MISSING;
    SC_HANDLE s = OpenServiceW(scm, g_services[i].name, SERVICE_QUERY_STATUS | SERVICE_QUERY_CONFIG);
    if (!s) { CloseServiceHandle(scm); return SVC_MISSING; }
    int state = SVC_STOPPED;
    SERVICE_STATUS_PROCESS ssp{};
    DWORD need = 0;
    if (QueryServiceStatusEx(s, SC_STATUS_PROCESS_INFO, (BYTE*)&ssp, sizeof ssp, &need) &&
        ssp.dwCurrentState != SERVICE_STOPPED && ssp.dwCurrentState != SERVICE_STOP_PENDING) {
        state = SVC_RUNNING;
    } else {
        QueryServiceConfigW(s, nullptr, 0, &need);
        std::vector<BYTE> buf(need ? need : 1);
        auto* cfg = (QUERY_SERVICE_CONFIGW*)buf.data();
        if (need && QueryServiceConfigW(s, cfg, need, &need) && cfg->dwStartType == SERVICE_DISABLED) state = SVC_DISABLED;
    }
    CloseServiceHandle(s);
    CloseServiceHandle(scm);
    return state;
}

bool ServiceWasDisabledByApp(int i) { return LoadAppDword(SvcKey(i).c_str(), 0) != 0; }

bool ServiceSet(int i, bool enable) {
    SC_HANDLE scm = OpenSCManagerW(nullptr, nullptr, SC_MANAGER_CONNECT);
    if (!scm) return false;
    SC_HANDLE s = OpenServiceW(scm, g_services[i].name,
                               SERVICE_CHANGE_CONFIG | SERVICE_STOP | SERVICE_START | SERVICE_QUERY_STATUS | SERVICE_QUERY_CONFIG);
    if (!s) { CloseServiceHandle(scm); return false; }
    bool ok = true;
    if (!enable) {
        // guarda o tipo de inicialização atual para restaurar depois
        DWORD need = 0;
        QueryServiceConfigW(s, nullptr, 0, &need);
        std::vector<BYTE> buf(need ? need : 1);
        auto* cfg = (QUERY_SERVICE_CONFIGW*)buf.data();
        if (need && QueryServiceConfigW(s, cfg, need, &need) && cfg->dwStartType != SERVICE_DISABLED)
            SaveAppDword(SvcKey(i).c_str(), cfg->dwStartType + 1);   // +1: 0 significa "nada salvo"
        ok = ChangeServiceConfigW(s, SERVICE_NO_CHANGE, SERVICE_DISABLED, SERVICE_NO_CHANGE,
                                  nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr) != FALSE;
        SERVICE_STATUS st{};
        ControlService(s, SERVICE_CONTROL_STOP, &st);
        for (int t = 0; t < 25; ++t) {   // espera até 5s parar
            SERVICE_STATUS_PROCESS ssp{};
            if (!QueryServiceStatusEx(s, SC_STATUS_PROCESS_INFO, (BYTE*)&ssp, sizeof ssp, &need) ||
                ssp.dwCurrentState == SERVICE_STOPPED) break;
            Sleep(200);
        }
    } else {
        DWORD saved = LoadAppDword(SvcKey(i).c_str(), 0);
        DWORD start = saved ? saved - 1 : g_services[i].defStart;
        if (start == SERVICE_DISABLED) start = g_services[i].defStart;
        ok = ChangeServiceConfigW(s, SERVICE_NO_CHANGE, start, SERVICE_NO_CHANGE,
                                  nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr) != FALSE;
        if (!StartServiceW(s, 0, nullptr) && GetLastError() != ERROR_SERVICE_ALREADY_RUNNING) {
            // serviços "manual/trigger" podem recusar início direto; ficam apenas reabilitados
        }
        SaveAppDword(SvcKey(i).c_str(), 0);
    }
    CloseServiceHandle(s);
    CloseServiceHandle(scm);
    return ok;
}

// ============================================================
//  Display
// ============================================================
DisplaySettings g_disp = {50, 6500, 100};

void DisplayLoad() {
    g_disp.saturation  = (int)LoadAppDword(L"DispSaturation", 50);
    g_disp.temperature = (int)LoadAppDword(L"DispTemperature", 6500);
    g_disp.gamma100    = (int)LoadAppDword(L"DispGamma", 100);
    if (g_disp.saturation < 0 || g_disp.saturation > 100) g_disp.saturation = 50;
    if (g_disp.temperature < 3000 || g_disp.temperature > 9000) g_disp.temperature = 6500;
    if (g_disp.gamma100 < 50 || g_disp.gamma100 > 200) g_disp.gamma100 = 100;
}
void DisplaySave() {
    SaveAppDword(L"DispSaturation", g_disp.saturation);
    SaveAppDword(L"DispTemperature", g_disp.temperature);
    SaveAppDword(L"DispGamma", g_disp.gamma100);
}
bool DisplayIsNeutral() { return g_disp.saturation == 50 && g_disp.temperature == 6500 && g_disp.gamma100 == 100; }

// Cor do corpo negro (aprox. Tanner Helland), normalizada para 6500K = branco
static void Kelvin(float k, float& r, float& g, float& b) {
    float t = k / 100.f;
    if (t <= 66) { r = 255; g = 99.4708025861f * logf(t) - 161.1195681661f; }
    else { r = 329.698727446f * powf(t - 60, -0.1332047592f); g = 288.1221695283f * powf(t - 60, -0.0755148492f); }
    if (t >= 66) b = 255;
    else if (t <= 19) b = 0;
    else b = 138.5177312231f * logf(t - 10) - 305.0447927307f;
    auto c = [](float v) { return v < 0 ? 0.f : (v > 255 ? 1.f : v / 255.f); };
    r = c(r); g = c(g); b = c(b);
}

static bool SetGammaAll(const WORD (*ramp)[256]) {
    bool any = false;
    DISPLAY_DEVICEW dd{};
    dd.cb = sizeof dd;
    for (DWORD i = 0; EnumDisplayDevicesW(nullptr, i, &dd, 0); ++i) {
        if (dd.StateFlags & DISPLAY_DEVICE_ATTACHED_TO_DESKTOP) {
            HDC dc = CreateDCW(nullptr, dd.DeviceName, nullptr, nullptr);
            if (dc) {
                if (SetDeviceGammaRamp(dc, (LPVOID)ramp)) any = true;
                DeleteDC(dc);
            }
        }
        dd.cb = sizeof dd;
    }
    return any;
}

// --- Saturação via Magnification API (efeito de cor em tela cheia, dura enquanto o app estiver aberto)
struct MagColorEffect { float transform[5][5]; };
typedef BOOL (WINAPI* MagInit_t)();
typedef BOOL (WINAPI* MagUninit_t)();
typedef BOOL (WINAPI* MagSetColor_t)(MagColorEffect*);
static HMODULE s_mag = nullptr;
static bool s_magOn = false;

static bool ApplySaturation(int sat) {
    float s = sat / 50.f;   // 1.0 = neutro
    if (!s_mag) s_mag = LoadLibraryW(L"Magnification.dll");
    if (!s_mag) return false;
    auto init = (MagInit_t)(void*)GetProcAddress(s_mag, "MagInitialize");
    auto uninit = (MagUninit_t)(void*)GetProcAddress(s_mag, "MagUninitialize");
    auto setc = (MagSetColor_t)(void*)GetProcAddress(s_mag, "MagSetFullscreenColorEffect");
    if (!init || !uninit || !setc) return false;
    if (fabsf(s - 1.f) < 0.001f) {
        if (s_magOn) {
            MagColorEffect id = {{{1, 0, 0, 0, 0}, {0, 1, 0, 0, 0}, {0, 0, 1, 0, 0}, {0, 0, 0, 1, 0}, {0, 0, 0, 0, 1}}};
            setc(&id);
            uninit();
            s_magOn = false;
        }
        return true;
    }
    if (!s_magOn) {
        if (!init()) return false;
        s_magOn = true;
    }
    const float w[3] = {0.2126f, 0.7152f, 0.0722f};
    MagColorEffect e{};
    for (int i = 0; i < 3; ++i)
        for (int j = 0; j < 3; ++j)
            e.transform[i][j] = (1 - s) * w[i] + (i == j ? s : 0);
    e.transform[3][3] = 1;
    e.transform[4][4] = 1;
    return setc(&e) != FALSE;
}

bool DisplayApply() {
    // Libera a faixa completa do gamma ramp (o Windows limita ajustes fortes por padrão)
    HKEY k;
    if (RegCreateKeyExW(HKEY_LOCAL_MACHINE, L"SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion\\ICM", 0, nullptr, 0,
                        KEY_SET_VALUE | KEY_WOW64_64KEY, nullptr, &k, nullptr) == ERROR_SUCCESS) {
        DWORD v = 256;
        RegSetValueExW(k, L"GdiIcmGammaRange", 0, REG_DWORD, (const BYTE*)&v, sizeof v);
        RegCloseKey(k);
    }
    float kr, kg, kb, nr, ng, nb;
    Kelvin((float)g_disp.temperature, kr, kg, kb);
    Kelvin(6500.f, nr, ng, nb);
    float mul[3] = {kr / nr, kg / ng, kb / nb};
    float mx = (std::max)(mul[0], (std::max)(mul[1], mul[2]));
    for (float& m : mul) m /= mx;  // nunca passa de 1.0
    float inv = 100.f / g_disp.gamma100;
    static WORD ramp[3][256];
    for (int i = 0; i < 256; ++i) {
        float v = powf(i / 255.f, inv);
        for (int c = 0; c < 3; ++c) {
            float o = v * mul[c] * 65535.f;
            ramp[c][i] = (WORD)(o < 0 ? 0 : (o > 65535 ? 65535 : o));
        }
    }
    bool ok = SetGammaAll(ramp);
    ok &= ApplySaturation(g_disp.saturation);
    return ok;
}

void DisplayShutdown() {
    if (s_magOn) ApplySaturation(50);
}

// ============================================================
//  Mira sobreposta (janela transparente, clique atravessa, sempre no topo)
//  Não lê nem altera nada do jogo: é só um desenho por cima da tela.
// ============================================================
CrossSettings g_cross = {XS_CROSSDOT, 0, 6, 2, 3, 1, 0};
const unsigned kCrossColors[] = {0x00FF4C, 0x00E5FF, 0xFF3B47, 0xFFE600, 0xFFFFFF, 0xFF28DC};
const int kCrossColorCount = sizeof(kCrossColors) / sizeof(kCrossColors[0]);

void CrossLoad() {
    g_cross.style   = (int)LoadAppDword(L"XStyle", XS_CROSSDOT);
    g_cross.color   = (int)LoadAppDword(L"XColor", 0);
    g_cross.size    = (int)LoadAppDword(L"XSize", 6);
    g_cross.thick   = (int)LoadAppDword(L"XThick", 2);
    g_cross.gap     = (int)LoadAppDword(L"XGap", 3);
    g_cross.outline = (int)LoadAppDword(L"XOutline", 1);
    g_cross.enabled = (int)LoadAppDword(L"XEnabled", 0);
    if (g_cross.style < 0 || g_cross.style >= XS_COUNT) g_cross.style = XS_CROSSDOT;
    if (g_cross.color < 0 || g_cross.color >= kCrossColorCount) g_cross.color = 0;
    g_cross.size = (std::max)(2, (std::min)(30, g_cross.size));
    g_cross.thick = (std::max)(1, (std::min)(6, g_cross.thick));
    g_cross.gap = (std::max)(0, (std::min)(12, g_cross.gap));
}
void CrossSave() {
    SaveAppDword(L"XStyle", g_cross.style);
    SaveAppDword(L"XColor", g_cross.color);
    SaveAppDword(L"XSize", g_cross.size);
    SaveAppDword(L"XThick", g_cross.thick);
    SaveAppDword(L"XGap", g_cross.gap);
    SaveAppDword(L"XOutline", g_cross.outline);
    SaveAppDword(L"XEnabled", g_cross.enabled);
}

void DrawCrosshair(Gdiplus::Graphics& g, float cx, float cy, const CrossSettings& s) {
    using namespace Gdiplus;
    unsigned rgb = kCrossColors[s.color];
    Color col(255, (BYTE)(rgb >> 16), (BYTE)(rgb >> 8), (BYTE)rgb);
    Color out(230, 0, 0, 0);
    SolidBrush fill(col), outline(out);
    float t = (float)s.thick, half = t / 2.f;
    std::vector<RectF> rects;
    bool arms = s.style == XS_CROSS || s.style == XS_CROSSDOT;
    bool dot = s.style == XS_DOT || s.style == XS_CROSSDOT || s.style == XS_CIRCLE;
    if (arms) {
        float g0 = (float)s.gap, L = (float)s.size;
        rects.push_back(RectF(cx - g0 - L, cy - half, L, t));   // esquerda
        rects.push_back(RectF(cx + g0, cy - half, L, t));       // direita
        rects.push_back(RectF(cx - half, cy - g0 - L, t, L));   // cima
        rects.push_back(RectF(cx - half, cy + g0, t, L));       // baixo
    }
    if (dot) {
        float d = s.style == XS_DOT ? (std::max)(2.f, t * 1.5f) : t;
        rects.push_back(RectF(cx - d / 2, cy - d / 2, d, d));
    }
    SmoothingMode sm = g.GetSmoothingMode();
    if (s.style == XS_CIRCLE) {
        g.SetSmoothingMode(SmoothingModeAntiAlias);
        float r = (float)s.size + s.gap;
        if (s.outline) { Pen po(out, t + 2); g.DrawEllipse(&po, cx - r, cy - r, r * 2, r * 2); }
        Pen pc(col, t);
        g.DrawEllipse(&pc, cx - r, cy - r, r * 2, r * 2);
    }
    g.SetSmoothingMode(SmoothingModeNone);   // linhas nítidas, pixel a pixel
    if (s.outline)
        for (auto& r : rects) g.FillRectangle(&outline, RectF(r.X - 1, r.Y - 1, r.Width + 2, r.Height + 2));
    for (auto& r : rects) g.FillRectangle(&fill, r);
    g.SetSmoothingMode(sm);
}

static HWND s_xwnd = nullptr;

void CrossApply(HINSTANCE inst) {
    using namespace Gdiplus;
    if (!g_cross.enabled) {
        if (s_xwnd) ShowWindow(s_xwnd, SW_HIDE);
        return;
    }
    if (!s_xwnd) {
        WNDCLASSEXW wc{};
        wc.cbSize = sizeof wc;
        wc.lpfnWndProc = DefWindowProcW;
        wc.hInstance = inst;
        wc.lpszClassName = L"PrecisionFixCrosshair";
        RegisterClassExW(&wc);
        s_xwnd = CreateWindowExW(WS_EX_LAYERED | WS_EX_TRANSPARENT | WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE,
                                 wc.lpszClassName, L"", WS_POPUP, 0, 0, 1, 1, nullptr, nullptr, inst, nullptr);
        if (!s_xwnd) return;
    }
    const int S = 128;
    BITMAPINFO bi{};
    bi.bmiHeader.biSize = sizeof bi.bmiHeader;
    bi.bmiHeader.biWidth = S;
    bi.bmiHeader.biHeight = -S;
    bi.bmiHeader.biPlanes = 1;
    bi.bmiHeader.biBitCount = 32;
    void* bits = nullptr;
    HDC sdc = GetDC(nullptr);
    HBITMAP dib = CreateDIBSection(sdc, &bi, DIB_RGB_COLORS, &bits, nullptr, 0);
    if (!dib) { ReleaseDC(nullptr, sdc); return; }
    memset(bits, 0, S * S * 4);
    {
        Bitmap bmp(S, S, S * 4, PixelFormat32bppPARGB, (BYTE*)bits);
        Graphics g(&bmp);
        DrawCrosshair(g, S / 2.f, S / 2.f, g_cross);
    }
    HDC mdc = CreateCompatibleDC(sdc);
    HGDIOBJ old = SelectObject(mdc, dib);
    // centro do monitor principal
    POINT pos = {GetSystemMetrics(SM_CXSCREEN) / 2 - S / 2, GetSystemMetrics(SM_CYSCREEN) / 2 - S / 2};
    SIZE sz = {S, S};
    POINT src = {0, 0};
    BLENDFUNCTION bf = {AC_SRC_OVER, 0, 255, AC_SRC_ALPHA};
    UpdateLayeredWindow(s_xwnd, sdc, &pos, &sz, mdc, &src, 0, &bf, ULW_ALPHA);
    SelectObject(mdc, old);
    DeleteDC(mdc);
    DeleteObject(dib);
    ReleaseDC(nullptr, sdc);
    ShowWindow(s_xwnd, SW_SHOWNOACTIVATE);
    CrossKeepOnTop();
}

void CrossKeepOnTop() {
    if (s_xwnd && IsWindowVisible(s_xwnd))
        SetWindowPos(s_xwnd, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
}

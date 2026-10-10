#include <winsock2.h>
#include <ws2tcpip.h>
#include "extras.h"
#include "tweaks.h"
#include <cmath>
#include <cwctype>
#include <algorithm>

static const wchar_t* kApp = L"Software\\PrecisionFix";

std::wstring LoadServerAddr() {
    wchar_t buf[256] = {};
    DWORD sz = sizeof(buf) - sizeof(wchar_t);
    if (RegGetValueW(HKEY_CURRENT_USER, kApp, L"ServerAddr", RRF_RT_REG_SZ, nullptr, buf, &sz) != ERROR_SUCCESS) return L"";
    return buf;
}

void SaveServerAddr(const std::wstring& addr) {
    HKEY k;
    if (RegCreateKeyExW(HKEY_CURRENT_USER, kApp, 0, nullptr, 0, KEY_SET_VALUE, nullptr, &k, nullptr) != ERROR_SUCCESS) return;
    RegSetValueExW(k, L"ServerAddr", 0, REG_SZ, (const BYTE*)addr.c_str(), (DWORD)((addr.size() + 1) * sizeof(wchar_t)));
    RegCloseKey(k);
}

bool ParseServerAddr(const std::wstring& in, std::wstring& host, int& port) {
    std::wstring s;
    for (wchar_t c : in) if (!iswspace(c) || !s.empty()) s += c;
    while (!s.empty() && iswspace(s.back())) s.pop_back();
    auto strip = [&](const std::wstring& pre) {
        if (s.size() >= pre.size() && _wcsnicmp(s.c_str(), pre.c_str(), pre.size()) == 0) s = s.substr(pre.size());
    };
    strip(L"fivem://connect/");
    strip(L"connect ");
    strip(L"http://");
    strip(L"https://");
    if (s.find(L"cfx.re/join") != std::wstring::npos || s.empty() || s.size() > 200) return false;
    size_t slash = s.find(L'/');
    if (slash != std::wstring::npos) s = s.substr(0, slash);
    port = 30120;
    size_t colon = s.rfind(L':');
    if (colon != std::wstring::npos) {
        port = _wtoi(s.c_str() + colon + 1);
        s = s.substr(0, colon);
    }
    if (port <= 0 || port > 65535 || s.empty()) return false;
    for (wchar_t c : s)
        if (!(iswalnum(c) || c == L'.' || c == L'-')) return false;
    host = s;
    return true;
}

std::vector<PingTarget> PingTargets() {
    std::vector<PingTarget> t;
    t.push_back({L"Cloudflare", L"1.1.1.1", 443});
    t.push_back({L"Google", L"8.8.8.8", 443});
    std::wstring host;
    int port;
    if (ParseServerAddr(LoadServerAddr(), host, port)) t.push_back({L"Seu servidor", host, port});
    return t;
}

static double NowMs() {
    LARGE_INTEGER f, c;
    QueryPerformanceFrequency(&f);
    QueryPerformanceCounter(&c);
    return c.QuadPart * 1000.0 / f.QuadPart;
}

static double TcpPing(const sockaddr* addr, int addrLen, int family) {
    SOCKET s = socket(family, SOCK_STREAM, IPPROTO_TCP);
    if (s == INVALID_SOCKET) return -1;
    u_long nb = 1;
    ioctlsocket(s, FIONBIO, &nb);
    double t0 = NowMs();
    connect(s, addr, addrLen);
    fd_set wr, ex;
    FD_ZERO(&wr); FD_SET(s, &wr);
    FD_ZERO(&ex); FD_SET(s, &ex);
    timeval tv{2, 0};
    int r = select(0, nullptr, &wr, &ex, &tv);
    double dt = NowMs() - t0;
    bool ok = r > 0 && FD_ISSET(s, &wr) && !FD_ISSET(s, &ex);
    closesocket(s);
    return ok ? dt : -1;
}

void RunPingTest(std::vector<PingTarget>& targets) {
    WSADATA wsa;
    WSAStartup(MAKEWORD(2, 2), &wsa);
    for (auto& t : targets) {
        addrinfoW hints{}, *res = nullptr;
        hints.ai_family = AF_UNSPEC;
        hints.ai_socktype = SOCK_STREAM;
        wchar_t portStr[16];
        swprintf(portStr, 16, L"%d", t.port);
        if (GetAddrInfoW(t.host.c_str(), portStr, &hints, &res) != 0 || !res) { t.avg = -1; t.loss = 100; continue; }
        const int N = 8;
        std::vector<double> ok;
        for (int i = 0; i < N; ++i) {
            double ms = TcpPing(res->ai_addr, (int)res->ai_addrlen, res->ai_family);
            if (ms >= 0) ok.push_back(ms);
            Sleep(60);
        }
        FreeAddrInfoW(res);
        t.loss = (int)((N - ok.size()) * 100 / N);
        if (ok.empty()) { t.avg = -1; t.jitter = -1; continue; }
        double sum = 0, jit = 0;
        for (size_t i = 0; i < ok.size(); ++i) {
            sum += ok[i];
            if (i) jit += fabs(ok[i] - ok[i - 1]);
        }
        t.avg = (int)lround(sum / ok.size());
        t.jitter = ok.size() > 1 ? (int)lround(jit / (ok.size() - 1)) : 0;
    }
    WSACleanup();
}

#include "updater.h"
#include <winhttp.h>
#include <vector>
#include <cwchar>
#include <cstring>

static const wchar_t* kRepoOwner = L"orenandev";
static const wchar_t* kRepoName = L"PrecisionFix";

std::wstring AppVersion() {
    const char* v = APP_VERSION;
    return std::wstring(v, v + strlen(v));
}

static std::wstring ExePath() {
    wchar_t buf[MAX_PATH];
    DWORD n = GetModuleFileNameW(nullptr, buf, MAX_PATH);
    return std::wstring(buf, n);
}

static bool HttpGet(const std::wstring& url, std::string& body, size_t maxBytes, bool api, volatile LONG* progress = nullptr) {
    URL_COMPONENTS uc{};
    uc.dwStructSize = sizeof uc;
    wchar_t host[256] = {}, path[2048] = {};
    uc.lpszHostName = host;
    uc.dwHostNameLength = 256;
    uc.lpszUrlPath = path;
    uc.dwUrlPathLength = 2048;
    if (!WinHttpCrackUrl(url.c_str(), 0, 0, &uc)) return false;

    std::wstring ua = L"PrecisionFix/" + AppVersion();
    HINTERNET s = WinHttpOpen(ua.c_str(), WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY, WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (!s) s = WinHttpOpen(ua.c_str(), WINHTTP_ACCESS_TYPE_DEFAULT_PROXY, WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (!s) return false;
    WinHttpSetTimeouts(s, 8000, 8000, 15000, 30000);
    bool ok = false;
    HINTERNET c = WinHttpConnect(s, host, uc.nPort, 0);
    if (c) {
        HINTERNET r = WinHttpOpenRequest(c, L"GET", path, nullptr, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES,
                                         uc.nScheme == INTERNET_SCHEME_HTTPS ? WINHTTP_FLAG_SECURE : 0);
        if (r) {
            const wchar_t* hdr = api ? L"Accept: application/vnd.github+json\r\n" : L"Accept: application/octet-stream\r\n";
            if (WinHttpSendRequest(r, hdr, (DWORD)-1L, WINHTTP_NO_REQUEST_DATA, 0, 0, 0) && WinHttpReceiveResponse(r, nullptr)) {
                DWORD status = 0, sz = sizeof status;
                WinHttpQueryHeaders(r, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER, WINHTTP_HEADER_NAME_BY_INDEX,
                                    &status, &sz, WINHTTP_NO_HEADER_INDEX);
                DWORD total = 0;
                sz = sizeof total;
                if (progress && !WinHttpQueryHeaders(r, WINHTTP_QUERY_CONTENT_LENGTH | WINHTTP_QUERY_FLAG_NUMBER, WINHTTP_HEADER_NAME_BY_INDEX,
                                                     &total, &sz, WINHTTP_NO_HEADER_INDEX)) total = 0;
                if (status == 200) {
                    ok = true;
                    for (;;) {
                        DWORD avail = 0;
                        if (!WinHttpQueryDataAvailable(r, &avail)) { ok = false; break; }
                        if (avail == 0) break;
                        size_t old = body.size();
                        if (old + avail > maxBytes) { ok = false; break; }
                        body.resize(old + avail);
                        DWORD read = 0;
                        if (!WinHttpReadData(r, &body[old], avail, &read)) { ok = false; break; }
                        body.resize(old + read);
                        if (progress && total) InterlockedExchange(progress, (LONG)(body.size() * 100 / total));
                    }
                }
            }
            WinHttpCloseHandle(r);
        }
        WinHttpCloseHandle(c);
    }
    WinHttpCloseHandle(s);
    return ok;
}

static bool JsonString(const std::string& js, const char* key, size_t from, std::string& out, size_t* pos = nullptr) {
    std::string k = std::string("\"") + key + "\"";
    size_t p = js.find(k, from);
    if (p == std::string::npos) return false;
    p = js.find(':', p + k.size());
    if (p == std::string::npos) return false;
    p = js.find('"', p);
    if (p == std::string::npos) return false;
    size_t e = js.find('"', p + 1);
    if (e == std::string::npos) return false;
    out = js.substr(p + 1, e - p - 1);
    if (pos) *pos = e;
    return true;
}

static std::vector<int> ParseVersion(std::wstring v) {
    if (!v.empty() && (v[0] == L'v' || v[0] == L'V')) v.erase(0, 1);
    std::vector<int> parts;
    int cur = 0;
    bool any = false;
    for (wchar_t c : v) {
        if (c >= L'0' && c <= L'9') { cur = cur * 10 + (c - L'0'); any = true; }
        else if (c == L'.') { parts.push_back(cur); cur = 0; any = false; }
        else break;
    }
    if (any) parts.push_back(cur);
    while (parts.size() < 3) parts.push_back(0);
    return parts;
}

static bool IsNewer(const std::wstring& remote, const std::wstring& local) {
    std::vector<int> a = ParseVersion(remote), b = ParseVersion(local);
    for (size_t i = 0; i < 3; ++i)
        if (a[i] != b[i]) return a[i] > b[i];
    return false;
}

bool CheckForUpdate(UpdateInfo& out) {
    std::wstring api = std::wstring(L"https://api.github.com/repos/") + kRepoOwner + L"/" + kRepoName + L"/releases/latest";
    std::string js;
    if (!HttpGet(api, js, 2 * 1024 * 1024, true)) return false;
    std::string tag;
    if (!JsonString(js, "tag_name", 0, tag)) return false;
    std::wstring wtag(tag.begin(), tag.end());
    if (!IsNewer(wtag, AppVersion())) return false;

    size_t pos = 0;
    std::string url;
    while (JsonString(js, "browser_download_url", pos, url, &pos)) {
        if (url.size() >= 16 && _stricmp(url.c_str() + url.size() - 16, "PrecisionFix.exe") == 0) {
            out.url.assign(url.begin(), url.end());
            out.version = (!wtag.empty() && (wtag[0] == L'v' || wtag[0] == L'V')) ? wtag.substr(1) : wtag;
            return true;
        }
    }
    return false;
}

bool DownloadUpdate(const UpdateInfo& info, std::wstring& newPath, volatile LONG* progress) {
    std::string data;
    if (!HttpGet(info.url, data, 64 * 1024 * 1024, false, progress)) return false;
    if (data.size() < 100 * 1024 || data[0] != 'M' || data[1] != 'Z') return false;
    newPath = ExePath() + L".new";
    HANDLE f = CreateFileW(newPath.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (f == INVALID_HANDLE_VALUE) return false;
    DWORD w = 0;
    bool ok = WriteFile(f, data.data(), (DWORD)data.size(), &w, nullptr) && w == data.size();
    CloseHandle(f);
    if (!ok) DeleteFileW(newPath.c_str());
    return ok;
}

bool InstallUpdateAndRestart(const std::wstring& newPath) {
    std::wstring exe = ExePath();
    std::wstring old = exe + L".old";
    DeleteFileW(old.c_str());
    if (!MoveFileExW(exe.c_str(), old.c_str(), MOVEFILE_REPLACE_EXISTING)) return false;
    if (!MoveFileExW(newPath.c_str(), exe.c_str(), MOVEFILE_REPLACE_EXISTING)) {
        MoveFileExW(old.c_str(), exe.c_str(), MOVEFILE_REPLACE_EXISTING);
        return false;
    }
    std::wstring cmd = L"\"" + exe + L"\" --updated";
    STARTUPINFOW si{};
    si.cb = sizeof si;
    PROCESS_INFORMATION pi{};
    if (!CreateProcessW(nullptr, &cmd[0], nullptr, nullptr, FALSE, 0, nullptr, nullptr, &si, &pi)) {
        MoveFileExW(exe.c_str(), newPath.c_str(), MOVEFILE_REPLACE_EXISTING);
        MoveFileExW(old.c_str(), exe.c_str(), MOVEFILE_REPLACE_EXISTING);
        return false;
    }
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
    return true;
}

void CleanupOldVersion() {
    std::wstring old = ExePath() + L".old";
    for (int i = 0; i < 10 && GetFileAttributesW(old.c_str()) != INVALID_FILE_ATTRIBUTES; ++i) {
        if (DeleteFileW(old.c_str())) break;
        Sleep(300);
    }
    DeleteFileW((ExePath() + L".new").c_str());
}

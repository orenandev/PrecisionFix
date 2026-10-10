#include <windows.h>
#include <windowsx.h>
#include <shellapi.h>
#include <dwmapi.h>
#include <objidl.h>
#include <gdiplus.h>
#include <string>
#include <vector>
#include <map>
#include <cmath>
#include <algorithm>
#include "tweaks.h"
#include "sysinfo.h"
#include "extras.h"
#include "updater.h"

using namespace Gdiplus;

static const wchar_t* kSiteUrl = L"https://usecodelabs.com/";

static const float W = 1040, H = 700;
static const float TOP_H = 58, SIDE_W = 236;
static const float CX = SIDE_W + 46, CW = W - (SIDE_W + 46) - 46;
static const float TITLE_Y = 64;
static const float VIEW_TOP = 128, VIEW_BOT = H - 10, VIEW_H = VIEW_BOT - VIEW_TOP;

enum { PG_DASH, PG_FIX, PG_KBD, PG_FIVEM, PG_PVP, PG_SVC, PG_DISP, PG_CFG, PG_COUNT };

enum {
    ID_MIN = 1, ID_CLOSE = 2,
    ID_NAV = 10,
    ID_MCHECK = 100,
    ID_KCHECK = 130,
    ID_QUICK = 150,
    ID_FMOPT = 170,
    ID_SVC = 180,
    ID_SWATCH = 200,
    ID_PRESET = 220,
    ID_SLIDER = 240,
    ID_STEP = 500,
    ID_PVPOPT = 260,
    ID_TOOL = 270,
    ID_PVP_MODE = 350, ID_PING = 351, ID_PASTE = 352, ID_POLL = 353, ID_UPDATE = 354, ID_REBOOT_PILL = 355, ID_COLOR_PICK = 356,
    ID_RESET_COLOR = 300, ID_REVERT_ALL = 301, ID_ENABLE_ALL = 302, ID_DISABLE_ALL = 303,
    ID_SITE = 304, ID_SITE_SIDE = 305, ID_SITE_BADGE = 306,
    ID_MODAL_CANCEL = 400, ID_MODAL_OK = 401,
    ID_CARD = 1000,
};

enum { MODAL_NONE, MODAL_ENABLE_ALL, MODAL_DISABLE_ALL, MODAL_REVERT, MODAL_REBOOT };
enum { JOB_ONE, JOB_ALL_REFS, JOB_REVERT, JOB_SVC, JOB_TOOL, JOB_PING };
enum { SRC_TWEAK, SRC_QUICK, SRC_KBD, SRC_PVP };

static const UINT WM_APP_DONE = WM_APP + 1;
static const UINT WM_APP_UPD_FOUND = WM_APP + 2;
static const UINT WM_APP_UPD_READY = WM_APP + 3;
static const UINT WM_APP_UPD_NONE = WM_APP + 4;
static const UINT_PTR TIMER_ANIM = 1, TIMER_BOOST = 2, TIMER_POLL = 4;

struct Theme { Color a, b; };
static const Theme kThemes[] = {
    {Color(255, 250, 250, 250), Color(255, 161, 161, 170)},
    {Color(255, 0, 229, 255),  Color(255, 56, 150, 255)},
    {Color(255, 255, 40, 220), Color(255, 140, 70, 255)},
    {Color(255, 255, 59, 71),  Color(255, 47, 140, 255)},
    {Color(255, 52, 211, 110), Color(255, 20, 184, 166)},
    {Color(255, 255, 176, 46), Color(255, 255, 112, 50)},
    {Color(255, 132, 60, 255), Color(255, 236, 72, 153)},
};
static const int kThemeCount = sizeof(kThemes) / sizeof(kThemes[0]);
static DWORD g_customColor = 0xA855F7;
static int g_theme = 0;

static Theme CurrentTheme() {
    if (g_theme < kThemeCount) return kThemes[g_theme];
    Color a(255, (BYTE)(g_customColor >> 16), (BYTE)(g_customColor >> 8), (BYTE)g_customColor);
    Color gray(255, 161, 161, 170);
    Color b(255, (BYTE)((a.GetR() + gray.GetR()) / 2), (BYTE)((a.GetG() + gray.GetG()) / 2), (BYTE)((a.GetB() + gray.GetB()) / 2));
    return {a, b};
}

static const Color cBg(255, 9, 9, 11);
static const Color cCard(255, 15, 15, 17);
static const Color cBorder(255, 36, 36, 40);
static const Color cText(255, 250, 250, 250);
static const Color cSub(255, 161, 161, 170);
static const Color cMuted(255, 113, 113, 122);
static const Color cOk(255, 52, 211, 153), cErr(255, 248, 113, 113), cWarn(255, 251, 191, 36);

struct Hit { int id; RectF r; bool click; };
struct ToggleState { bool on = false; float anim = 0; };

static HWND g_hwnd;
static float g_scale = 1.f;
static bool g_admin = false;

static int g_page = PG_DASH;
static float g_pageT = 1;
static float g_scroll[PG_COUNT], g_scrollT[PG_COUNT], g_contentH[PG_COUNT];

static float g_mx = -1, g_my = -1;
static bool g_tracking = false;
static int g_pressId = 0;
static std::vector<Hit> g_hits;
static std::map<int, float> g_hov;
static bool g_clip = false;
static RectF g_clipR;

static std::vector<ToggleState> g_tw, g_qk, g_kb, g_pv, g_fm(1);
static HINSTANCE g_inst;
static std::vector<PingTarget> g_ping;
static bool g_pollActive = false;
static std::vector<LONGLONG> g_pollTimes;
static LONGLONG g_pollStart = 0;
static int g_pollHz = 0, g_pollLive = 0;
static HANDLE g_mutex = nullptr;
static void InstallPendingUpdate();
static bool g_rebootPending = false;
static bool g_updChecking = false;
static std::wstring g_updPendingPath;
static std::vector<int> g_svcState;
static int g_drag = -1;
static ULONGLONG g_lastDispApply = 0;
static float g_acc[3], g_acc2[3];

static bool g_busy = false;
static int g_busyKind = -1, g_busyIdx = -1;
static float g_pulse = 0;

static std::wstring g_toast;
static int g_toastKind = 0;
static float g_toastA = 0;
static ULONGLONG g_toastUntil = 0;

static int g_modal = MODAL_NONE, g_modalShown = MODAL_NONE;
static float g_modalA = 0;

static PrivateFontCollection* g_pfc[4];
static FontFamily* g_fam[4];
static int g_famStyle[4];

static Font *fLogo, *fNav, *fPage, *fCardT, *fTglT, *fDesc, *fSection, *fBtn, *fSmall, *fTag,
            *fLabel, *fValue, *fToast, *fModalT, *fRow, *fBig, *fMono, *fCheck;

static void LoadEmbeddedFont(int slot, int resId) {
    HRSRC r = FindResourceW(nullptr, MAKEINTRESOURCEW(resId), RT_RCDATA);
    if (!r) return;
    HGLOBAL h = LoadResource(nullptr, r);
    void* p = LockResource(h);
    DWORD sz = SizeofResource(nullptr, r);
    if (!p || !sz) return;
    g_pfc[slot] = new PrivateFontCollection();
    if (g_pfc[slot]->AddMemoryFont(p, (INT)sz) != Ok) return;
    FontFamily* fam = new FontFamily();
    INT found = 0;
    g_pfc[slot]->GetFamilies(1, fam, &found);
    if (found < 1) { delete fam; return; }
    g_fam[slot] = fam;
    g_famStyle[slot] = fam->IsStyleAvailable(FontStyleRegular) ? FontStyleRegular
                     : fam->IsStyleAvailable(FontStyleBold)    ? FontStyleBold : FontStyleRegular;
}

static Font* MakeFont(int weight, float px) {
    if (g_fam[weight]) {
        Font* f = new Font(g_fam[weight], px, g_famStyle[weight], UnitPixel);
        if (f->GetLastStatus() == Ok) return f;
        delete f;
    }
    const wchar_t* fam = weight == 2 ? L"Segoe UI Semibold" : L"Segoe UI";
    Font* f = new Font(fam, px, weight == 3 ? FontStyleBold : FontStyleRegular, UnitPixel);
    if (f->GetLastStatus() != Ok) { delete f; f = new Font(L"Segoe UI", px, FontStyleRegular, UnitPixel); }
    return f;
}

static void CreateFonts() {
    for (int i = 0; i < 4; ++i) LoadEmbeddedFont(i, 101 + i);
    fLogo    = MakeFont(3, 15);
    fNav     = MakeFont(1, 14.5f);
    fPage    = MakeFont(2, 28);
    fCardT   = MakeFont(2, 18);
    fTglT    = MakeFont(2, 16);
    fDesc    = MakeFont(0, 13.5f);
    fSection = MakeFont(2, 11);
    fBtn     = MakeFont(2, 14);
    fSmall   = MakeFont(0, 12);
    fTag     = MakeFont(2, 9.5f);
    fLabel   = MakeFont(2, 10.5f);
    fValue   = MakeFont(2, 15);
    fToast   = MakeFont(1, 13);
    fModalT  = MakeFont(3, 20);
    fRow     = MakeFont(1, 14);
    fBig     = MakeFont(3, 22);
    fMono    = new Font(L"Consolas", 15, FontStyleBold, UnitPixel);
    fCheck   = MakeFont(2, 10.5f);
}

static Color Mix(const Color& a, const Color& b, float t) {
    t = t < 0 ? 0 : (t > 1 ? 1 : t);
    auto L = [t](BYTE x, BYTE y) { return (BYTE)(x + (y - x) * t + 0.5f); };
    return Color(L(a.GetA(), b.GetA()), L(a.GetR(), b.GetR()), L(a.GetG(), b.GetG()), L(a.GetB(), b.GetB()));
}
static Color Alpha(const Color& c, float a) {
    a = a < 0 ? 0 : (a > 1 ? 1 : a);
    return Color((BYTE)(c.GetA() * a), c.GetR(), c.GetG(), c.GetB());
}
static Color Acc(float a = 1)  { return Alpha(Color(255, (BYTE)g_acc[0], (BYTE)g_acc[1], (BYTE)g_acc[2]), a); }
static Color Acc2(float a = 1) { return Alpha(Color(255, (BYTE)g_acc2[0], (BYTE)g_acc2[1], (BYTE)g_acc2[2]), a); }
static float Clampf(float v, float lo, float hi) { return v < lo ? lo : (v > hi ? hi : v); }
static RectF Inflate(const RectF& r, float d) { return RectF(r.X - d, r.Y - d, r.Width + 2 * d, r.Height + 2 * d); }

static void RoundRect(GraphicsPath& p, const RectF& r, float rad) {
    rad = (std::min)(rad, (std::min)(r.Width, r.Height) / 2);
    float d = rad * 2;
    p.AddArc(r.X, r.Y, d, d, 180, 90);
    p.AddArc(r.X + r.Width - d, r.Y, d, d, 270, 90);
    p.AddArc(r.X + r.Width - d, r.Y + r.Height - d, d, d, 0, 90);
    p.AddArc(r.X, r.Y + r.Height - d, d, d, 90, 90);
    p.CloseFigure();
}
static void FillRound(Graphics& g, const RectF& r, float rad, const Brush& b) {
    GraphicsPath p; RoundRect(p, r, rad); g.FillPath(&b, &p);
}
static void FillRound(Graphics& g, const RectF& r, float rad, const Color& c) {
    SolidBrush b(c); FillRound(g, r, rad, b);
}
static void StrokeRound(Graphics& g, const RectF& r, float rad, const Color& c, float w = 1.f) {
    GraphicsPath p; RoundRect(p, r, rad); Pen pen(c, w); g.DrawPath(&pen, &p);
}
static void Glow(Graphics& g, const RectF& r, float rad, const Color& c, float strength, int layers = 4) {
    if (strength <= 0.01f) return;
    for (int i = layers; i >= 1; --i)
        FillRound(g, Inflate(r, i * 2.5f), rad + i * 2.5f, Alpha(c, 0.05f * strength));
}

static StringFormat* Typo() {
    static StringFormat* f = nullptr;
    if (!f) {
        f = new StringFormat(StringFormat::GenericTypographic());
        f->SetFormatFlags(f->GetFormatFlags() | StringFormatFlagsMeasureTrailingSpaces);
    }
    return f;
}
static void Text(Graphics& g, const std::wstring& s, Font* f, const Color& c, float x, float y) {
    SolidBrush b(c);
    g.DrawString(s.c_str(), -1, f, PointF(x, y), Typo(), &b);
}
static float TextW(Graphics& g, const std::wstring& s, Font* f) {
    static std::map<std::pair<Font*, std::wstring>, float> cache;
    auto key = std::make_pair(f, s);
    auto it = cache.find(key);
    if (it != cache.end()) return it->second;
    RectF bb;
    g.MeasureString(s.c_str(), -1, f, PointF(0, 0), Typo(), &bb);
    cache[key] = bb.Width;
    return bb.Width;
}
static float CharW(Graphics& g, Font* f, wchar_t c) {
    static std::map<std::pair<Font*, wchar_t>, float> cache;
    auto key = std::make_pair(f, c);
    auto it = cache.find(key);
    if (it != cache.end()) return it->second;
    RectF bb;
    g.MeasureString(&c, 1, f, PointF(0, 0), Typo(), &bb);
    cache[key] = bb.Width;
    return bb.Width;
}
static void TextBox(Graphics& g, const std::wstring& s, Font* f, const Color& c, const RectF& r,
                    bool wrap = true, StringAlignment h = StringAlignmentNear, StringAlignment v = StringAlignmentNear) {
    StringFormat sf(StringFormat::GenericTypographic());
    sf.SetTrimming(wrap ? StringTrimmingEllipsisWord : StringTrimmingEllipsisCharacter);
    sf.SetFormatFlags(wrap ? 0 : StringFormatFlagsNoWrap);
    sf.SetAlignment(h);
    sf.SetLineAlignment(v);
    SolidBrush b(c);
    g.DrawString(s.c_str(), -1, f, r, &sf, &b);
}
static float SpacedW(Graphics& g, const wchar_t* s, Font* f, float sp) {
    float w = 0;
    int n = 0;
    for (const wchar_t* c = s; *c; ++c, ++n) w += CharW(g, f, *c);
    return w + sp * (n > 0 ? n - 1 : 0);
}
static float Spaced(Graphics& g, const wchar_t* s, Font* f, const Color& c, float x, float y, float sp) {
    SolidBrush b(c);
    for (const wchar_t* ch = s; *ch; ++ch) {
        if (*ch != L' ') g.DrawString(ch, 1, f, PointF(x, y), Typo(), &b);
        x += CharW(g, f, *ch) + sp;
    }
    return x - sp;
}

static bool IsModalId(int id) { return id == ID_MODAL_CANCEL || id == ID_MODAL_OK; }

static void AddHit(int id, RectF r, bool click) {
    if (g_clip) {
        RectF c;
        if (!RectF::Intersect(c, r, g_clipR)) return;
        r = c;
    }
    g_hits.push_back({id, r, click});
}
static bool MouseIn(int id) {
    if (g_mx < 0) return false;
    if (g_modal && !IsModalId(id)) return false;
    for (auto& h : g_hits)
        if (h.id == id && h.r.Contains(g_mx, g_my)) return true;
    return false;
}
static float Hov(int id) {
    auto it = g_hov.find(id);
    if (it == g_hov.end()) { g_hov[id] = 0; return 0; }
    return it->second;
}
static int TopClick() {
    if (g_mx < 0) return 0;
    for (int i = (int)g_hits.size() - 1; i >= 0; --i) {
        const Hit& h = g_hits[i];
        if (!h.click || (g_modal && !IsModalId(h.id))) continue;
        if (h.r.Contains(g_mx, g_my)) return h.id;
    }
    return 0;
}

enum { BTN_ACCENT, BTN_DANGER, BTN_APPLIED, BTN_GHOST, BTN_ORANGE };

static float Luma(const Color& c) { return (0.299f * c.GetR() + 0.587f * c.GetG() + 0.114f * c.GetB()) / 255.f; }

static void Arrow(Graphics& g, float x, float cy, const Color& c) {
    Pen p(c, 1.7f);
    p.SetStartCap(LineCapRound);
    p.SetEndCap(LineCapRound);
    g.DrawLine(&p, x, cy, x + 10, cy);
    g.DrawLine(&p, x + 6, cy - 4, x + 10, cy);
    g.DrawLine(&p, x + 6, cy + 4, x + 10, cy);
}

static void Check(Graphics& g, float x, float cy, const Color& c) {
    Pen p(c, 1.8f);
    p.SetStartCap(LineCapRound);
    p.SetEndCap(LineCapRound);
    p.SetLineJoin(LineJoinRound);
    PointF pts[3] = {PointF(x, cy), PointF(x + 3.5f, cy + 3.5f), PointF(x + 10, cy - 3.5f)};
    g.DrawLines(&p, pts, 3);
}

static void Button(Graphics& g, RectF r, const wchar_t* label, int id, int kind, bool enabled = true, bool arrow = false) {
    AddHit(id, r, enabled);
    float h = enabled ? Hov(id) : 0;
    float rad = r.Height / 2;
    r.Y -= h * 1.2f;

    Color fill, border, tc;
    bool check = false;
    switch (kind) {
    case BTN_ACCENT:
        fill = Mix(Acc(), Color(255, 255, 255, 255), 0.12f * h);
        border = Color(0, 0, 0, 0);
        tc = Luma(Acc()) > 0.55f ? Color(255, 10, 10, 11) : Color(255, 255, 255, 255);
        Glow(g, r, rad, Acc(), (enabled ? 0.35f + 0.65f * h : 0), 3);
        break;
    case BTN_DANGER:
        fill = Mix(Color(255, 220, 38, 38), Color(255, 239, 68, 68), h);
        border = Color(0, 0, 0, 0);
        tc = Color(255, 255, 255, 255);
        Glow(g, r, rad, Color(255, 239, 68, 68), enabled ? 0.3f + 0.7f * h : 0, 3);
        break;
    case BTN_ORANGE:
        fill = Mix(Color(255, 234, 88, 12), Color(255, 249, 115, 22), h);
        border = Color(0, 0, 0, 0);
        tc = Color(255, 255, 255, 255);
        Glow(g, r, rad, Color(255, 249, 115, 22), enabled ? 0.4f + 0.6f * h : 0, 3);
        break;
    case BTN_APPLIED:
        fill = Mix(Color(255, 14, 14, 16), Color(255, 22, 22, 25), h);
        border = Mix(Acc(0.35f), Acc(0.8f), h);
        tc = Acc();
        check = h < 0.5f;
        break;
    default:
        fill = Mix(Color(255, 12, 12, 14), Color(255, 24, 24, 27), h);
        border = Mix(Color(255, 39, 39, 42), Color(255, 82, 82, 91), h);
        tc = cText;
        break;
    }
    if (!enabled) { fill = Mix(fill, Color(255, 30, 30, 33), 0.7f); tc = Mix(tc, cMuted, 0.7f); }

    FillRound(g, r, rad, fill);
    if (border.GetA()) StrokeRound(g, r, rad, border, 1.f);

    float tw = TextW(g, label, fBtn);
    float extra = (arrow || check) ? 18.f : 0.f;
    float x = r.X + (r.Width - tw - extra) / 2;
    if (check) { Check(g, x, r.Y + r.Height / 2, tc); x += 18; }
    Text(g, label, fBtn, tc, x, r.Y + (r.Height - 17) / 2);
    if (arrow) Arrow(g, x + tw + 8, r.Y + r.Height / 2, tc);
}

static void CardBg(Graphics& g, const RectF& r, int id, bool click = false) {
    AddHit(id, r, click);
    float h = Hov(id);
    Glow(g, r, 16, Acc(), h * 0.35f, 3);
    FillRound(g, r, 16, Mix(cCard, Color(255, 21, 21, 24), h));
    StrokeRound(g, r, 16, Mix(cBorder, Acc(0.55f), h), 1.f);
}

static void Padlock(Graphics& g, float cx, float cy, float s, const Color& c) {
    Pen p(c, 1.4f * s);
    g.DrawArc(&p, cx - 2.6f * s, cy - 5.2f * s, 5.2f * s, 5.6f * s, 180, 180);
    g.DrawLine(&p, cx - 2.6f * s, cy - 2.4f * s, cx - 2.6f * s, cy - 0.8f * s);
    g.DrawLine(&p, cx + 2.6f * s, cy - 2.4f * s, cx + 2.6f * s, cy - 0.8f * s);
    FillRound(g, RectF(cx - 4 * s, cy - 1.2f * s, 8 * s, 6.4f * s), 1.4f * s, c);
}

static void DrawLockedToggle(Graphics& g, const RectF& r, float hov) {
    Color track = Mix(Mix(Acc(), Color(255, 24, 24, 27), 0.55f), Color(255, 40, 40, 44), 0.25f * hov);
    FillRound(g, r, r.Height / 2, track);
    StrokeRound(g, r, r.Height / 2, Mix(Color(255, 46, 46, 51), Acc(0.5f), 0.3f), 1.f);
    float d = r.Height - 6;
    float kx = r.X + r.Width - 3 - d, ky = r.Y + 3;
    SolidBrush kb(Color(255, 200, 200, 206));
    g.FillEllipse(&kb, kx, ky, d, d);
    Padlock(g, kx + d / 2, ky + d / 2 + 0.4f, 1.f, Color(255, 40, 40, 46));
}

static void SectionHeader(Graphics& g, const wchar_t* text, float y) {
    float end = Spaced(g, text, fSection, Mix(cSub, Acc(), 0.6f), CX, y, 2.2f);
    float ly = y + 7.5f;
    LinearGradientBrush lb(PointF(end + 14, ly), PointF(CX + CW, ly), Color(255, 39, 39, 42), Color(0, 39, 39, 42));
    g.FillRectangle(&lb, end + 14, ly, CX + CW - end - 14, 1.f);
}

static void CodeLabsMark(Graphics& g, float x, float y, float s, const Color& c) {
    static const float L[8][2] = {{159, 0}, {371, 0}, {371, 104}, {274, 265}, {371, 422}, {371, 529}, {159, 529}, {0, 265}};
    static const float R[6][2] = {{678, 0}, {889, 0}, {889, 109}, {638, 529}, {425, 529}, {425, 422}};
    float k = 16.f * s / 529.f;
    PointF lp[8], rp[6];
    for (int i = 0; i < 8; ++i) lp[i] = PointF(x + L[i][0] * k, y + L[i][1] * k);
    for (int i = 0; i < 6; ++i) rp[i] = PointF(x + R[i][0] * k, y + R[i][1] * k);
    SolidBrush b(c);
    g.FillPolygon(&b, lp, 8);
    g.FillPolygon(&b, rp, 6);
}

struct Ref { int src; int idx; const wchar_t* label; };
static std::vector<Ref> g_mouseRefs, g_kbdRefs;

static Tweak& RefTweak(const Ref& r) {
    switch (r.src) {
        case SRC_TWEAK: return g_tweaks[r.idx];
        case SRC_QUICK: return g_quick[r.idx];
        case SRC_KBD:   return g_kbd[r.idx];
        default:        return g_pvp[r.idx];
    }
}
static ToggleState& RefState(const Ref& r) {
    switch (r.src) {
        case SRC_TWEAK: return g_tw[r.idx];
        case SRC_QUICK: return g_qk[r.idx];
        case SRC_KBD:   return g_kb[r.idx];
        default:        return g_pv[r.idx];
    }
}

static int FindQuick(const wchar_t* name) {
    for (int i = 0; i < g_quickCount; ++i)
        if (wcscmp(g_quick[i].name, name) == 0) return i;
    return -1;
}

static void BuildRefs() {
    struct { int src; const wchar_t* name; const wchar_t* label; } mouse[] = {
        {SRC_QUICK, L"Precisão do Mouse (1:1)", L"MOUSE PRECISION"},
        {SRC_TWEAK, L"Tracking",                L"TRACKING"},
        {SRC_TWEAK, L"Sensi",                   L"SENSI"},
        {SRC_TWEAK, L"Flick",                   L"FLICK"},
        {SRC_TWEAK, L"0.5ms",                   L"0.5MS"},
        {SRC_TWEAK, L"Registros Mouse",         L"REGISTROS MOUSE"},
        {SRC_TWEAK, L"Islc",                    L"ISLC"},
        {SRC_TWEAK, L"Sensi X/Y",               L"SENSI X/Y"},
        {SRC_TWEAK, L"Flags",                   L"FLAGS"},
        {SRC_TWEAK, L"Accessibility",           L"ACCESSIBILITY"},
    };
    for (auto& m : mouse) {
        int i = m.src == SRC_QUICK ? FindQuick(m.name) : FindTweak(m.name);
        if (i >= 0) g_mouseRefs.push_back({m.src, i, m.label});
    }
    const wchar_t* kl[] = {L"PERFIL DE PRECISÃO", L"FILTER KEYS OFF", L"ATRASO MÍNIMO", L"REPETIÇÃO MÁXIMA",
                           L"PRIORIDADE EM JOGO", L"AJUSTES DE REGISTRO", L"USB SELETIVA OFF", L"STICKY/TOGGLE OFF"};
    for (int i = 0; i < g_kbdCount && i < 8; ++i) g_kbdRefs.push_back({SRC_KBD, i, kl[i]});
}

static const wchar_t* kPageNames[PG_COUNT] = {
    L"Dashboard", L"Precision Fix", L"Teclado Fix", L"FiveM", L"PvP", L"Serviços", L"Display", L"Configurações"};

static std::vector<PointF> ClipHalfPlane(const std::vector<PointF>& poly, float a, float b, float c) {
    std::vector<PointF> out;
    for (size_t i = 0; i < poly.size(); ++i) {
        const PointF& p = poly[i];
        const PointF& q = poly[(i + 1) % poly.size()];
        float fp = a * p.X + b * p.Y - c, fq = a * q.X + b * q.Y - c;
        if (fp <= 0) out.push_back(p);
        if ((fp < 0 && fq > 0) || (fp > 0 && fq < 0)) {
            float t = fp / (fp - fq);
            out.push_back(PointF(p.X + (q.X - p.X) * t, p.Y + (q.Y - p.Y) * t));
        }
    }
    return out;
}

static void FiveMGlyph(Graphics& g, float cx, float cy, float s, const Color& c) {
    const float hgt = 18.f * s, wid = hgt * 0.92f;
    auto P = [&](float ux, float uy) { return PointF(cx + (ux / 100.f - 0.5f) * wid, cy + (uy / 100.f - 0.5f) * hgt); };
    const float legs[2][5][2] = {
        {{35, 0}, {50.4f, 0}, {50.4f, 30}, {28, 100}, {0, 100}},
        {{50, 0}, {65, 0}, {100, 100}, {72, 100}, {50, 30}},
    };
    const float bands[4][2] = {{-1000, 58.5f}, {61.5f, 66}, {69, 73.5f}, {76.5f, 1000}};
    SolidBrush b(c);
    for (auto& leg : legs) {
        std::vector<PointF> base;
        for (auto& pt : leg) base.push_back(PointF(pt[0], pt[1]));
        for (auto& band : bands) {
            std::vector<PointF> piece = ClipHalfPlane(base, 1, 1, band[1]);
            piece = ClipHalfPlane(piece, -1, -1, -band[0]);
            if (piece.size() < 3) continue;
            std::vector<PointF> pts;
            for (auto& u : piece) pts.push_back(P(u.X, u.Y));
            g.FillPolygon(&b, pts.data(), (INT)pts.size());
        }
    }
}

static void NavIcon(Graphics& g, int which, float cx, float cy, const Color& c) {
    Pen p(c, 1.6f);
    p.SetLineJoin(LineJoinRound);
    p.SetStartCap(LineCapRound);
    p.SetEndCap(LineCapRound);
    SolidBrush b(c);
    switch (which) {
    case PG_DASH:
        for (int i = 0; i < 4; ++i) {
            GraphicsPath gp;
            RoundRect(gp, RectF(cx - 8 + (i % 2) * 9.5f, cy - 8 + (i / 2) * 9.5f, 6.5f, 6.5f), 1.6f);
            g.DrawPath(&p, &gp);
        }
        break;
    case PG_FIX: {
        GraphicsPath gp;
        RoundRect(gp, RectF(cx - 5.5f, cy - 8, 11, 16), 5.5f);
        g.DrawPath(&p, &gp);
        g.DrawLine(&p, cx, cy - 6, cx, cy - 3);
        break;
    }
    case PG_KBD: {
        GraphicsPath gp;
        RoundRect(gp, RectF(cx - 9, cy - 6, 18, 12), 2.5f);
        g.DrawPath(&p, &gp);
        for (int i = 0; i < 4; ++i) g.FillRectangle(&b, cx - 6 + i * 3.6f, cy - 3, 1.6f, 1.6f);
        g.DrawLine(&p, cx - 4, cy + 2.5f, cx + 4, cy + 2.5f);
        break;
    }
    case PG_FIVEM:
        FiveMGlyph(g, cx, cy, 0.95f, c);
        break;
    case PG_PVP: {
        g.DrawLine(&p, cx - 7, cy - 7, cx + 6, cy + 6);
        g.DrawLine(&p, cx + 7, cy - 7, cx - 6, cy + 6);
        g.DrawLine(&p, cx + 3, cy + 7, cx + 7, cy + 3);
        g.DrawLine(&p, cx - 3, cy + 7, cx - 7, cy + 3);
        break;
    }
    case PG_SVC:
        for (int i = 0; i < 9; ++i) g.FillRectangle(&b, cx - 7 + (i % 3) * 5.2f, cy - 7 + (i / 3) * 5.2f, 3.6f, 3.6f);
        break;
    case PG_DISP: {
        GraphicsPath gp;
        RoundRect(gp, RectF(cx - 9, cy - 7, 18, 12), 2);
        g.DrawPath(&p, &gp);
        g.DrawLine(&p, cx - 4, cy + 8, cx + 4, cy + 8);
        g.DrawLine(&p, cx, cy + 5, cx, cy + 8);
        break;
    }
    case PG_CFG: {
        PointF pts[32];
        for (int i = 0; i < 32; ++i) {
            float a = (i + 0.5f) * 6.2831853f / 32.f;
            float rad = ((i / 2) % 2 == 0) ? 8.2f : 6.0f;
            pts[i] = PointF(cx + cosf(a) * rad, cy + sinf(a) * rad);
        }
        g.DrawPolygon(&p, pts, 32);
        g.DrawEllipse(&p, cx - 2.6f, cy - 2.6f, 5.2f, 5.2f);
        break;
    }
    }
}

static void DrawBackground(Graphics& g) {
    SolidBrush bg(cBg);
    g.FillRectangle(&bg, 0.f, 0.f, W, H);
    {
        SolidBrush d(Color(16, 255, 255, 255));
        for (float y = 11; y < H; y += 24)
            for (float x = SIDE_W + 11; x < W; x += 24)
                g.FillRectangle(&d, x, y, 2.f, 2.f);
    }
    {
        GraphicsPath gp;
        gp.AddEllipse(CX - 180.f, -260.f, 980.f, 640.f);
        PathGradientBrush pg(&gp);
        pg.SetCenterColor(Color(14, 255, 255, 255));
        Color surround(0, 255, 255, 255);
        INT n = 1;
        pg.SetSurroundColors(&surround, &n);
        g.FillPath(&pg, &gp);
    }
    SolidBrush side(Color(255, 9, 9, 11));
    g.FillRectangle(&side, 0.f, 0.f, SIDE_W, H);
    LinearGradientBrush div(PointF(0, 20), PointF(0, TOP_H + 10), Color(0, 32, 32, 36), Color(255, 32, 32, 36));
    div.SetWrapMode(WrapModeTileFlipX);
    g.FillRectangle(&div, SIDE_W, 20.f, 1.f, TOP_H - 10);
    SolidBrush divs(Color(255, 32, 32, 36));
    g.FillRectangle(&divs, SIDE_W, TOP_H + 10, 1.f, H);
}

static void DrawChrome(Graphics& g) {
    float x = Spaced(g, L"PRECISION", fLogo, cText, 28, 21, 1.8f);
    Spaced(g, L"FIX", fLogo, Acc(), x + 10, 21, 1.8f);

    for (int i = 0; i < PG_COUNT; ++i) {
        RectF r(14, TOP_H + 4 + i * 48.f, SIDE_W - 28, 42);
        AddHit(ID_NAV + i, r, true);
        float h = Hov(ID_NAV + i);
        bool act = g_page == i;
        if (act) {
            LinearGradientBrush lb(PointF(r.X, 0), PointF(r.X + r.Width, 0), Acc(0.12f), Acc(0.02f));
            FillRound(g, r, 12, lb);
            StrokeRound(g, r, 12, Acc(0.10f));
            RectF bar(r.X, r.Y + 10, 3, r.Height - 20);
            Glow(g, bar, 1.5f, Acc(), 0.8f, 3);
            FillRound(g, bar, 1.5f, Acc());
        } else if (h > 0.01f) {
            FillRound(g, r, 12, Color((BYTE)(9 * h), 255, 255, 255));
        }
        Color ic = act ? Acc() : Mix(cMuted, cText, h);
        Color tc = act ? Acc() : Mix(cSub, cText, h);
        NavIcon(g, i, r.X + 30, r.Y + r.Height / 2, ic);
        Text(g, kPageNames[i], fNav, tc, r.X + 54, r.Y + 11);
    }

    if (g_rebootPending) {
        RectF rp(14, H - 156, SIDE_W - 28, 34);
        AddHit(ID_REBOOT_PILL, rp, true);
        float h = Hov(ID_REBOOT_PILL);
        FillRound(g, rp, 17, Mix(Color(255, 32, 26, 10), Color(255, 46, 36, 12), h));
        StrokeRound(g, rp, 17, Alpha(cWarn, 0.45f + 0.3f * h));
        SolidBrush wd(cWarn);
        g.FillEllipse(&wd, rp.X + 14, rp.Y + 13, 8.f, 8.f);
        Text(g, L"Reinício pendente", fSmall, Color(255, 250, 220, 150), rp.X + 30, rp.Y + 9);
        Arrow(g, rp.X + rp.Width - 26, rp.Y + 17, Alpha(cWarn, 0.8f));
    }

    float fy = H - 112;
    SolidBrush dot(g_admin ? cOk : cErr);
    g.FillEllipse(&dot, 30.f, fy + 5, 7.f, 7.f);
    Text(g, g_admin ? L"Modo administrador" : L"Sem privilégios de admin", fSmall, cSub, 46, fy);

    RectF cr(14, H - 84, SIDE_W - 28, 66);
    AddHit(ID_SITE_SIDE, cr, true);
    float ch = Hov(ID_SITE_SIDE);
    FillRound(g, cr, 14, Mix(Color(255, 14, 14, 16), Color(255, 22, 22, 25), ch));
    StrokeRound(g, cr, 14, Mix(cBorder, Color(255, 70, 70, 78), ch));
    CodeLabsMark(g, cr.X + 16, cr.Y + 25, 0.95f, cText);
    Text(g, L"Desenvolvido por", fSmall, cMuted, cr.X + 50, cr.Y + 14);
    Text(g, L"CodeLabs", fRow, cText, cr.X + 50, cr.Y + 31);
    Arrow(g, cr.X + cr.Width - 30, cr.Y + cr.Height / 2, Mix(cMuted, cText, ch));

    struct { int id; float x; Color c; } dots[] = {
        {ID_MIN, W - 50, Color(255, 245, 184, 46)},
        {ID_CLOSE, W - 28, Color(255, 255, 77, 77)},
    };
    for (auto& d : dots) {
        RectF hr(d.x - 9, 20, 18, 18);
        AddHit(d.id, hr, true);
        float h = Hov(d.id);
        float rr = 6.5f + h * 0.8f;
        Glow(g, RectF(d.x - rr, 29 - rr, rr * 2, rr * 2), rr, d.c, h, 2);
        SolidBrush b(d.c);
        g.FillEllipse(&b, d.x - rr, 29 - rr, rr * 2, rr * 2);
        if (h > 0.3f) {
            Pen gp(Color((BYTE)(200 * h), 60, 20, 10), 1.3f);
            if (d.id == ID_MIN) g.DrawLine(&gp, d.x - 3, 29.f, d.x + 3, 29.f);
            else { g.DrawLine(&gp, d.x - 2.6f, 26.4f, d.x + 2.6f, 31.6f); g.DrawLine(&gp, d.x + 2.6f, 26.4f, d.x - 2.6f, 31.6f); }
        }
    }
}

static Color Hsv(float h, float s, float v) {
    h = fmodf(h, 360.f) / 60.f;
    int i = (int)h;
    float f = h - i, p = v * (1 - s), q = v * (1 - s * f), t = v * (1 - s * (1 - f));
    float r, g, b;
    switch (i) {
        case 0: r = v; g = t; b = p; break;
        case 1: r = q; g = v; b = p; break;
        case 2: r = p; g = v; b = t; break;
        case 3: r = p; g = q; b = v; break;
        case 4: r = t; g = p; b = v; break;
        default: r = v; g = p; b = q; break;
    }
    return Color(255, (BYTE)(r * 255), (BYTE)(g * 255), (BYTE)(b * 255));
}

static const float MOUSE_W = 160, MOUSE_H = 262;
static const float KBD_W = 310, KBD_H = 118;

static void DrawMouseArt(Graphics& g, const Color& acc) {
    float cx = MOUSE_W / 2;
    {
        GraphicsPath sp;
        sp.AddEllipse(18.f, MOUSE_H - 30, MOUSE_W - 36, 26.f);
        PathGradientBrush pg(&sp);
        pg.SetCenterColor(Color(140, 0, 0, 0));
        Color sc(0, 0, 0, 0);
        INT n = 1;
        pg.SetSurroundColors(&sc, &n);
        g.FillPath(&pg, &sp);
    }
    GraphicsPath body;
    body.AddBezier(cx, 4.f, cx + 42, 4.f, cx + 68, 34.f, cx + 70, 92.f);
    body.AddBezier(cx + 70, 92.f, cx + 73, 150.f, cx + 74, 198.f, cx + 54, 234.f);
    body.AddBezier(cx + 54, 234.f, cx + 38, 254.f, cx - 38, 254.f, cx - 54, 234.f);
    body.AddBezier(cx - 54, 234.f, cx - 74, 198.f, cx - 73, 150.f, cx - 70, 92.f);
    body.AddBezier(cx - 70, 92.f, cx - 68, 34.f, cx - 42, 4.f, cx, 4.f);
    body.CloseFigure();
    LinearGradientBrush fill(PointF(0, 0), PointF(0, MOUSE_H), Color(255, 30, 30, 34), Color(255, 12, 12, 14));
    g.FillPath(&fill, &body);
    {
        GraphicsState st = g.Save();
        g.SetClip(&body);
        GraphicsPath hl;
        hl.AddEllipse(cx - 70, 10.f, 70.f, 220.f);
        PathGradientBrush pg(&hl);
        pg.SetCenterColor(Color(22, 255, 255, 255));
        Color sc(0, 255, 255, 255);
        INT n = 1;
        pg.SetSurroundColors(&sc, &n);
        g.FillPath(&pg, &hl);
        Pen seam(Color(255, 6, 6, 8), 1.6f);
        g.DrawLine(&seam, cx, 4.f, cx, 104.f);
        GraphicsPath arc;
        arc.AddBezier(cx - 72, 100.f, cx - 30, 116.f, cx + 30, 116.f, cx + 72, 100.f);
        g.DrawPath(&seam, &arc);
        g.Restore(st);
    }
    Pen edge(Color(255, 46, 46, 52), 1.2f);
    g.DrawPath(&edge, &body);
    FillRound(g, RectF(cx - 74, 112, 5, 26), 2.5f, Color(255, 26, 26, 30));
    FillRound(g, RectF(cx - 74, 142, 5, 26), 2.5f, Color(255, 26, 26, 30));
    RectF wheel(cx - 8, 28, 16, 46);
    FillRound(g, wheel, 8, Color(255, 8, 8, 10));
    StrokeRound(g, wheel, 8, Color(255, 50, 50, 56));
    RectF strip(cx - 2.5f, 33, 5, 36);
    for (int i = 4; i >= 1; --i) FillRound(g, Inflate(strip, i * 2.f), 2.5f + i * 2, Alpha(acc, 0.07f));
    FillRound(g, strip, 2.5f, acc);
    FillRound(g, RectF(cx - 4, 82, 8, 13), 3, Color(255, 20, 20, 23));
    float lw = 27.f * 1.55f, lh = 16.f * 1.55f;
    for (int i = 3; i >= 1; --i) {
        GraphicsPath gp;
        gp.AddEllipse(cx - lw / 2 - i * 8.f, 162 - i * 8.f, lw + i * 16.f, lh + i * 16.f);
        SolidBrush gb(Alpha(acc, 0.05f));
        g.FillPath(&gb, &gp);
    }
    CodeLabsMark(g, cx - lw / 2, 162, 1.55f, acc);
}

static void DrawKeyboardArt(Graphics& g) {
    RectF body(2, 2, KBD_W - 4, KBD_H - 4);
    for (int i = 0; i < 24; ++i) {
        RectF seg(body.X + i * body.Width / 24, body.Y + body.Height - 6, body.Width / 24 + 1, 8);
        SolidBrush b(Alpha(Hsv(i * 15.f, 0.9f, 1), 0.35f));
        g.FillRectangle(&b, seg);
    }
    LinearGradientBrush fb(PointF(0, body.Y), PointF(0, body.Y + body.Height), Color(255, 24, 24, 27), Color(255, 14, 14, 16));
    FillRound(g, body, 9, fb);
    StrokeRound(g, body, 9, Color(255, 48, 48, 54));

    static const float rows[5][16] = {
        {1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 2, 0},
        {1.5f, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1.5f, 0},
        {1.75f, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 2.25f, 0},
        {2.25f, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 2.75f, 0},
        {1.25f, 1.25f, 1.25f, 6.25f, 1.25f, 1.25f, 1.25f, 1.25f, 0},
    };
    float pad = 9;
    float u = (KBD_W - 4 - pad * 2) / 15.f;
    for (int r = 0; r < 5; ++r) {
        float x = body.X + pad, y = body.Y + pad + r * u;
        for (int k = 0; rows[r][k] > 0; ++k) {
            float w = rows[r][k] * u;
            RectF key(x + 1.2f, y + 1.2f, w - 2.4f, u - 2.4f);
            Color rgb = Hsv((x + w / 2) / KBD_W * 300.f, 0.85f, 1.f);
            FillRound(g, Inflate(key, 1.2f), 3.5f, Alpha(rgb, 0.45f));
            LinearGradientBrush kb(PointF(0, key.Y), PointF(0, key.Y + key.Height), Color(255, 38, 38, 43), Color(255, 22, 22, 25));
            FillRound(g, RectF(key.X, key.Y, key.Width, key.Height - 1.5f), 2.5f, kb);
            SolidBrush lg(Alpha(rgb, 0.9f));
            g.FillRectangle(&lg, key.X + 3, key.Y + 2.5f, 3.f, 1.3f);
            x += w;
        }
    }
}

struct ArtCache { Bitmap* bmp = nullptr; unsigned key = 0; };

static void DrawArt(Graphics& g, ArtCache& cache, unsigned key, float x, float y, float w, float h, void (*fn)(Graphics&, const Color&)) {
    int pw = (int)ceilf(w * g_scale), ph = (int)ceilf(h * g_scale);
    if (!cache.bmp || cache.key != key) {
        delete cache.bmp;
        cache.bmp = new Bitmap(pw, ph, PixelFormat32bppPARGB);
        Graphics bg(cache.bmp);
        bg.SetSmoothingMode(SmoothingModeAntiAlias);
        bg.SetPixelOffsetMode(PixelOffsetModeHalf);
        bg.Clear(Color(0, 0, 0, 0));
        bg.ScaleTransform(g_scale, g_scale);
        fn(bg, Acc());
        cache.key = key;
    }
    GraphicsState st = g.Save();
    g.ResetTransform();
    g.SetInterpolationMode(InterpolationModeNearestNeighbor);
    g.DrawImage(cache.bmp, Rect((INT)(x * g_scale), (INT)(y * g_scale), pw, ph));
    g.Restore(st);
}

static unsigned AccKey() { return ((unsigned)g_acc[0] << 16) | ((unsigned)g_acc[1] << 8) | (unsigned)g_acc[2]; }

static bool Offscreen(const RectF& r) { return r.Y > VIEW_BOT || r.Y + r.Height < VIEW_TOP - 10; }

static void CheckBox(Graphics& g, const RectF& r, float t, float hov, bool busy) {
    Glow(g, r, 6, Acc(), t * 0.7f, 3);
    Color off = Mix(Color(255, 16, 16, 19), Color(255, 24, 24, 28), hov);
    FillRound(g, r, 6, Mix(off, Acc(), t));
    Color bd = Mix(Mix(Color(255, 52, 52, 58), Color(255, 96, 96, 104), hov), Acc(), t);
    if (busy) bd = Mix(bd, Acc(), 0.5f + 0.5f * sinf(g_pulse));
    StrokeRound(g, r, 6, bd, 1.2f);
    if (t > 0.05f) {
        Color ck = Luma(Acc()) > 0.6f ? Color(255, 10, 10, 11) : Color(255, 255, 255, 255);
        Check(g, r.X + 5, r.Y + r.Height / 2, Alpha(ck, t));
    }
}

static void CheckRow(Graphics& g, float edgeX, float cy, bool labelLeft, const Ref& ref, int id, bool busy) {
    const float box = 22, gap = 12;
    float lw = SpacedW(g, ref.label, fCheck, 1.2f);
    RectF br = labelLeft ? RectF(edgeX - box, cy - box / 2, box, box) : RectF(edgeX, cy - box / 2, box, box);
    RectF hit = labelLeft ? RectF(br.X - gap - lw - 8, cy - 16, lw + gap + box + 12, 32)
                          : RectF(br.X - 4, cy - 16, lw + gap + box + 12, 32);
    AddHit(id, hit, !g_busy);
    float h = Hov(id);
    ToggleState& st = RefState(ref);
    Color tc = Mix(Mix(cSub, cText, h), cText, st.anim);
    float lx = labelLeft ? br.X - gap - lw : br.X + box + gap;
    Spaced(g, ref.label, fCheck, tc, lx, cy - 7, 1.2f);
    CheckBox(g, br, st.anim, h, busy);
}

static void DescribeBox(Graphics& g, float y, const std::vector<Ref>& refs, int baseId) {
    int hov = -1;
    for (int i = 0; i < (int)refs.size(); ++i)
        if (MouseIn(baseId + i)) hov = i;
    if (hov < 0) return;
    RectF r(CX, y, CW, 96);
    FillRound(g, r, 16, cCard);
    StrokeRound(g, r, 16, cBorder);
    const Tweak& t = RefTweak(refs[hov]);
    Text(g, t.name, fTglT, cText, r.X + 24, r.Y + 16);
    float tx = r.X + 24 + TextW(g, t.name, fTglT) + 10;
    if (t.reboot) {
        const wchar_t* tag = L"REINÍCIO";
        RectF tr(tx, r.Y + 18, SpacedW(g, tag, fTag, 0.8f) + 14, 17);
        FillRound(g, tr, 8.5f, Color(255, 24, 24, 28));
        StrokeRound(g, tr, 8.5f, Color(255, 46, 46, 52));
        Spaced(g, tag, fTag, cSub, tr.X + 7, tr.Y + 3, 0.8f);
    }
    TextBox(g, t.desc, fDesc, cSub, RectF(r.X + 24, r.Y + 44, r.Width - 48, 22), false);
    Text(g, std::wstring(L"O que muda: ") + t.tech, fSmall, cMuted, r.X + 24, r.Y + 68);
}

static void PageSubtitle(Graphics& g, const wchar_t* s, float slide) {
    Text(g, s, fDesc, cMuted, CX + 1, TITLE_Y + 40 + slide);
}

static void Tile(Graphics& g, const RectF& r, int id, const wchar_t* label, const std::wstring& value, const std::wstring& detail) {
    CardBg(g, r, id);
    Spaced(g, label, fLabel, cMuted, r.X + 18, r.Y + 15, 1.6f);
    TextBox(g, value, fValue, cText, RectF(r.X + 18, r.Y + 31, r.Width - 36, 22), false);
    TextBox(g, detail, fSmall, cSub, RectF(r.X + 18, r.Y + 54, r.Width - 36, 18), false);
}

static bool BusyFor(int src, int idx) {
    if (!g_busy) return false;
    return g_busyKind == JOB_ALL_REFS || g_busyKind == JOB_REVERT || (g_busyKind == JOB_ONE && g_busyIdx == src * 1000 + idx);
}

static float PageDashboard(Graphics& g, float y) {
    SectionHeader(g, L"SEU SISTEMA", y);
    y += 30;
    float tw = (CW - 14) / 2, th = 82;
    Tile(g, RectF(CX, y, tw, th), ID_CARD + 1, L"PROCESSADOR", g_sys.cpu, g_sys.cpuDetail);
    Tile(g, RectF(CX + tw + 14, y, tw, th), ID_CARD + 2, L"MEMÓRIA RAM", g_sys.ramTotal, g_sys.ramDetail);
    y += th + 14;
    Tile(g, RectF(CX, y, tw, th), ID_CARD + 3, L"PLACA DE VÍDEO", g_sys.gpu, L"Adaptador principal");
    Tile(g, RectF(CX + tw + 14, y, tw, th), ID_CARD + 4, L"MONITOR", g_sys.display, L"Monitor principal");
    y += th + 14;
    Tile(g, RectF(CX, y, tw, th), ID_CARD + 5, L"SISTEMA", g_sys.os, g_sys.osDetail);
    Tile(g, RectF(CX + tw + 14, y, tw, th), ID_CARD + 6, L"PLACA-MÃE", g_sys.board, L"Placa-mãe");
    y += th + 10;
    return y;
}

static ArtCache g_mouseArt, g_kbdArt;

static float PageMouse(Graphics& g, float y) {
    float cx = CX + CW / 2;
    float rowsH = 6 * 42.f;
    float top = y + 10;
    DrawArt(g, g_mouseArt, AccKey(), cx - MOUSE_W / 2, top, MOUSE_W, MOUSE_H, DrawMouseArt);
    float r0 = top + (MOUSE_H - rowsH) / 2 + 21;
    int n = (int)g_mouseRefs.size(), nl = (n + 1) / 2;
    for (int i = 0; i < n; ++i) {
        bool left = i < nl;
        int row = left ? i : i - nl;
        float off = left ? 0.f : (nl - (n - nl)) * 21.f;
        float cy = r0 + row * 42.f + off;
        const Ref& rf = g_mouseRefs[i];
        CheckRow(g, left ? cx - 112 : cx + 112, cy, left, rf, ID_MCHECK + i, BusyFor(rf.src, rf.idx));
    }
    y = top + MOUSE_H + 18;
    DescribeBox(g, y, g_mouseRefs, ID_MCHECK);
    return y + 96 + 10;
}

static void KbdArtFn(Graphics& g, const Color&) { DrawKeyboardArt(g); }

static float PageKeyboard(Graphics& g, float y) {
    float cx = CX + CW / 2;
    float rowsH = 4 * 46.f;
    float top = y + 20;
    DrawArt(g, g_kbdArt, 1, cx - KBD_W / 2, top + (rowsH - KBD_H) / 2, KBD_W, KBD_H, KbdArtFn);
    for (int i = 0; i < (int)g_kbdRefs.size(); ++i) {
        bool left = i < 4;
        float cy = top + 23 + (i % 4) * 46.f;
        const Ref& rf = g_kbdRefs[i];
        CheckRow(g, left ? cx - KBD_W / 2 - 18 : cx + KBD_W / 2 + 18, cy, left, rf, ID_KCHECK + i, BusyFor(rf.src, rf.idx));
    }
    y = top + rowsH + 34;
    DescribeBox(g, y, g_kbdRefs, ID_KCHECK);
    return y + 96 + 10;
}

static float PageFiveM(Graphics& g, float y) {
    SectionHeader(g, L"FERRAMENTAS", y);
    y += 30;
    struct { const wchar_t* t; const wchar_t* d; const wchar_t* btn; } tools[TOOL_COUNT] = {
        {L"Limpar cache do FiveM", L"Apaga tudo em data, menos a game-storage", L"Limpar"},
        {L"Limpar logs e crashes", L"Remove relatórios de erro antigos", L"Limpar"},
        {L"Limpar cache de DNS", L"Resolve falhas ao conectar no servidor", L"Limpar"},
        {L"Limpar temporários", L"Esvazia a pasta %TEMP% do Windows", L"Limpar"},
        {L"Abrir pasta do FiveM", L"FiveM Application Data no Explorer", L"Abrir"},
    };
    float tw = (CW - 14) / 2, th = 84;
    for (int i = 0; i < TOOL_COUNT; ++i) {
        RectF c(CX + (i % 2) * (tw + 14), y + (i / 2) * (th + 14), tw, th);
        if (Offscreen(c)) continue;
        CardBg(g, c, ID_CARD + 100 + i);
        TextBox(g, tools[i].t, fTglT, cText, RectF(c.X + 20, c.Y + 18, c.Width - 20 - 116, 24), false);
        TextBox(g, tools[i].d, fSmall, cSub, RectF(c.X + 20, c.Y + 46, c.Width - 20 - 110, 32));
        bool busyMe = g_busy && g_busyKind == JOB_TOOL && g_busyIdx == i;
        Button(g, RectF(c.X + c.Width - 20 - 92, c.Y + (th - 38) / 2, 92, 38), busyMe ? L"..." : tools[i].btn,
               ID_TOOL + i, BTN_GHOST, !g_busy);
    }
    y += 3 * (th + 14) + 16;

    SectionHeader(g, L"DESEMPENHO", y);
    y += 30;
    RectF c(CX, y, CW, 72);
    CardBg(g, c, ID_FMOPT, true);
    g_fm[0].on = g_fivem.autoBoost;
    CheckBox(g, RectF(c.X + 22, c.Y + 25, 22, 22), g_fm[0].anim, Hov(ID_FMOPT), false);
    Text(g, L"Boost automático ao detectar o jogo", fTglT, cText, c.X + 62, c.Y + 16);
    TextBox(g, L"Força prioridade alta no processo do FiveM enquanto o painel estiver aberto", fSmall, cSub,
            RectF(c.X + 62, c.Y + 42, c.Width - 84, 18), false);
    return y + 72 + 12;
}

static float PageServices(Graphics& g, float y) {
    for (int i = 0; i < g_serviceCount; ++i) {
        RectF r(CX, y, CW, 70);
        if (Offscreen(r)) { y += 70 + 12; continue; }
        int st = i < (int)g_svcState.size() ? g_svcState[i] : SVC_MISSING;
        bool missing = st == SVC_MISSING;
        CardBg(g, r, ID_CARD + 120 + i);
        wchar_t nm[96];
        swprintf(nm, 96, L"[ %d. %ls ]", i + 1, g_services[i].name);
        Text(g, nm, fMono, cText, r.X + 22, r.Y + 15);
        Text(g, g_services[i].desc, fSmall, cMuted, r.X + 23, r.Y + 41);

        const wchar_t* label = st == SVC_RUNNING ? L"RUNNING" : st == SVC_STOPPED ? L"STOPPED" : st == SVC_DISABLED ? L"DISABLED" : L"AUSENTE";
        Color bc = st == SVC_RUNNING ? Color(255, 74, 222, 128) : st == SVC_DISABLED ? Color(255, 248, 113, 113) : cSub;
        float bw = SpacedW(g, label, fTag, 1.f) + 22;
        float right = st == SVC_RUNNING ? 50.f : 96.f;
        RectF badge(r.X + r.Width - 22 - right - 18 - bw, r.Y + (r.Height - 22) / 2, bw, 22);
        FillRound(g, badge, 5, Alpha(bc, 0.10f));
        StrokeRound(g, badge, 5, Alpha(bc, 0.35f));
        Spaced(g, label, fTag, bc, badge.X + 11, badge.Y + 5.5f, 1.f);

        bool busyMe = g_busy && ((g_busyKind == JOB_SVC && g_busyIdx == i) || g_busyKind == JOB_REVERT);
        RectF act(r.X + r.Width - 22 - 50, r.Y + (r.Height - 26) / 2, 50, 26);
        if (st == SVC_RUNNING) {
            AddHit(ID_SVC + i, act, true);
            DrawLockedToggle(g, act, Hov(ID_SVC + i));
        }
        else if (!missing) Button(g, RectF(r.X + r.Width - 22 - 96, r.Y + (r.Height - 38) / 2, 96, 38), busyMe ? L"..." : L"Ativar",
                                  ID_SVC + i, BTN_ACCENT, !g_busy);
        y += 70 + 12;
    }
    return y;
}

struct Preset { const wchar_t* name; const wchar_t* desc; int sat, temp, gam; };
static const Preset kPresets[] = {
    {L"Padrão",      L"Volta tudo ao neutro: cores e brilho originais.",      50, 6500, 100},
    {L"Vibrante",    L"Cores mais vivas para destacar inimigos.",              75, 6500, 100},
    {L"Cinema",      L"Tom quente e suave para assistir.",                     45, 5200, 95},
    {L"FiveM Night", L"Para servidores noturnos: clareia e esfria a imagem.",  55, 7200, 125},
    {L"Competitivo", L"Máximo de visibilidade e contraste.",                   70, 6800, 115},
};
static const int kPresetCount = sizeof(kPresets) / sizeof(kPresets[0]);
struct SliderDef { const wchar_t* name; int lo, hi, step; };
static const SliderDef kSliders[3] = {{L"Saturação", 0, 100, 1}, {L"Temperatura", 3000, 9000, 100}, {L"Gama", 50, 200, 5}};
static RectF g_sliderTrack[3];

static int* SliderVal(int k) { return k == 0 ? &g_disp.saturation : k == 1 ? &g_disp.temperature : &g_disp.gamma100; }
static std::wstring SliderText(int k) {
    wchar_t b[32];
    if (k == 0) swprintf(b, 32, L"%d %%", g_disp.saturation);
    else if (k == 1) swprintf(b, 32, L"%d K", g_disp.temperature);
    else swprintf(b, 32, L"%.2f", g_disp.gamma100 / 100.f);
    return b;
}

static float SliderRow(Graphics& g, int k, float x, float ry, float w) {
    Text(g, kSliders[k].name, fRow, cText, x, ry + 6);
    float bx = x + w;
    RectF plus(bx - 28, ry, 28, 28), val(bx - 28 - 8 - 76, ry, 76, 28), minus(bx - 28 - 8 - 76 - 8 - 28, ry, 28, 28);
    for (int s = 0; s < 2; ++s) {
        RectF br = s ? plus : minus;
        int id = ID_STEP + k * 2 + s;
        AddHit(id, br, true);
        float h = Hov(id);
        FillRound(g, br, 7, Mix(Color(255, 18, 18, 21), Color(255, 30, 30, 34), h));
        StrokeRound(g, br, 7, Mix(cBorder, Color(255, 82, 82, 91), h));
        Pen pp(cText, 1.5f);
        float mx = br.X + 14, my = br.Y + 14;
        g.DrawLine(&pp, mx - 4, my, mx + 4, my);
        if (s) g.DrawLine(&pp, mx, my - 4, mx, my + 4);
    }
    FillRound(g, val, 7, Color(255, 12, 12, 14));
    StrokeRound(g, val, 7, cBorder);
    TextBox(g, SliderText(k), fSmall, cText, val, false, StringAlignmentCenter, StringAlignmentCenter);

    RectF track(x, ry + 50, w, 4);
    g_sliderTrack[k] = track;
    AddHit(ID_SLIDER + k, RectF(track.X - 8, track.Y - 12, track.Width + 16, 28), true);
    float t = (float)(*SliderVal(k) - kSliders[k].lo) / (kSliders[k].hi - kSliders[k].lo);
    FillRound(g, track, 2, Color(255, 39, 39, 42));
    FillRound(g, RectF(track.X, track.Y, track.Width * t, 4), 2, Acc(0.75f));
    float kx = track.X + track.Width * t, ky = track.Y + 2;
    float h = (g_drag == k) ? 1.f : Hov(ID_SLIDER + k);
    float kr = 7 + h * 1.5f;
    Glow(g, RectF(kx - kr, ky - kr, kr * 2, kr * 2), kr, Acc(), h, 3);
    SolidBrush kb(Color(255, 250, 250, 250));
    g.FillEllipse(&kb, kx - kr, ky - kr, kr * 2, kr * 2);
    Pen kp(Color(255, 9, 9, 11), 2.5f);
    g.DrawEllipse(&kp, kx - kr + 3, ky - kr + 3, kr * 2 - 6, kr * 2 - 6);
    return 84;
}

static float PageDisplay(Graphics& g, float y) {
    SectionHeader(g, L"PRESETS", y);
    y += 30;
    float pw = (CW - 28) / 3, ph = 84;
    for (int i = 0; i < kPresetCount; ++i) {
        RectF r(CX + (i % 3) * (pw + 14), y + (i / 3) * (ph + 14), pw, ph);
        bool sel = g_disp.saturation == kPresets[i].sat && g_disp.temperature == kPresets[i].temp && g_disp.gamma100 == kPresets[i].gam;
        CardBg(g, r, ID_PRESET + i, true);
        if (sel) StrokeRound(g, r, 16, Acc(0.8f), 1.2f);
        Text(g, kPresets[i].name, fTglT, cText, r.X + 18, r.Y + 16);
        if (sel) Check(g, r.X + r.Width - 30, r.Y + 26, Acc());
        TextBox(g, kPresets[i].desc, fSmall, cSub, RectF(r.X + 18, r.Y + 42, r.Width - 36, 36));
    }
    y += 2 * (ph + 14) + 16;

    RectF card(CX, y, CW, 3 * 84.f + 26);
    FillRound(g, card, 16, cCard);
    StrokeRound(g, card, 16, cBorder);
    for (int k = 0; k < 3; ++k) SliderRow(g, k, card.X + 24, card.Y + 22 + k * 84.f, card.Width - 48);
    y += card.Height + 14;
    return y;
}

static float PagePvP(Graphics& g, float y) {
    float hw = (CW - 16) / 2, hh = 236;
    RectF c1(CX, y, hw, hh);
    CardBg(g, c1, ID_CARD + 91);
    Text(g, L"Teste de Conexão", fCardT, cText, c1.X + 22, c1.Y + 20);
    Text(g, L"Ping, jitter e perda de pacotes", fSmall, cMuted, c1.X + 22, c1.Y + 47);
    std::vector<PingTarget> rows = g_ping.empty() ? PingTargets() : g_ping;
    float ry = c1.Y + 76;
    for (auto& t : rows) {
        Text(g, t.name, fRow, cText, c1.X + 22, ry);
        std::wstring v;
        Color vc = cMuted;
        if (g_busy && g_busyKind == JOB_PING) v = L"testando...";
        else if (g_ping.empty()) v = L"—";
        else if (t.avg < 0) { v = L"sem resposta"; vc = cErr; }
        else {
            wchar_t b[64];
            swprintf(b, 64, L"%d ms  ·  ±%d  ·  %d%%", t.avg, t.jitter, t.loss);
            v = b;
            vc = t.avg < 40 ? cOk : t.avg < 90 ? cWarn : cErr;
        }
        float vw = TextW(g, v, fSmall);
        Text(g, v, fSmall, vc, c1.X + c1.Width - 22 - vw, ry + 2);
        ry += 30;
    }
    if (rows.size() < 3) Text(g, L"Copie o IP:porta do servidor e clique em \"Colar IP\"", fSmall, cMuted, c1.X + 22, ry + 2);
    float bw = (hw - 44 - 10) / 2;
    Button(g, RectF(c1.X + 22, c1.Y + hh - 22 - 42, bw, 42), L"Colar IP", ID_PASTE, BTN_GHOST, !g_busy);
    Button(g, RectF(c1.X + 22 + bw + 10, c1.Y + hh - 22 - 42, bw, 42), L"Testar", ID_PING, BTN_ACCENT, !g_busy, true);

    RectF c2(CX + hw + 16, y, hw, hh);
    CardBg(g, c2, ID_CARD + 92);
    Text(g, L"Polling Rate do Mouse", fCardT, cText, c2.X + 22, c2.Y + 20);
    Text(g, L"Confere se o mouse entrega o Hz prometido", fSmall, cMuted, c2.X + 22, c2.Y + 47);
    int shown = g_pollActive ? g_pollLive : g_pollHz;
    wchar_t hz[32];
    if (shown > 0) swprintf(hz, 32, L"%d", shown); else wcscpy(hz, L"—");
    float nw = TextW(g, hz, fPage);
    Text(g, hz, fPage, g_pollActive ? Acc() : cText, c2.X + 22, c2.Y + 86);
    Text(g, L"Hz", fRow, cSub, c2.X + 22 + nw + 8, c2.Y + 100);
    const wchar_t* st = g_pollActive ? L"Mova o mouse em círculos rápidos..." :
                        g_pollHz > 0 ? (g_pollHz >= 900 ? L"Ótimo para PvP" : g_pollHz >= 450 ? L"Bom · 1000 Hz é o ideal para PvP"
                                                                              : L"Baixo · aumente o polling no software do mouse")
                                     : L"Clique em Iniciar e mova o mouse";
    Text(g, st, fSmall, cSub, c2.X + 22, c2.Y + 132);
    Button(g, RectF(c2.X + 22, c2.Y + hh - 22 - 42, hw - 44, 42), g_pollActive ? L"Testando..." : L"Iniciar teste",
           ID_POLL, BTN_ACCENT, !g_pollActive, !g_pollActive);
    y += hh + 28;

    SectionHeader(g, L"AJUSTES DE FPS E REDE", y);
    y += 30;
    for (int i = 0; i < g_pvpCount; ++i) {
        RectF c(CX, y, CW, 72);
        if (Offscreen(c)) { y += 72 + 12; continue; }
        CardBg(g, c, ID_PVPOPT + i, !g_busy);
        bool busyOne = BusyFor(SRC_PVP, i);
        CheckBox(g, RectF(c.X + 22, c.Y + 25, 22, 22), g_pv[i].anim, Hov(ID_PVPOPT + i), busyOne);
        Text(g, g_pvp[i].name, fTglT, cText, c.X + 62, c.Y + 16);
        if (g_pvp[i].reboot) {
            float tx = c.X + 62 + TextW(g, g_pvp[i].name, fTglT) + 10;
            const wchar_t* tag = L"REINÍCIO";
            RectF tr(tx, c.Y + 18, SpacedW(g, tag, fTag, 0.8f) + 14, 17);
            FillRound(g, tr, 8.5f, Color(255, 24, 24, 28));
            StrokeRound(g, tr, 8.5f, Color(255, 46, 46, 52));
            Spaced(g, tag, fTag, cSub, tr.X + 7, tr.Y + 3, 0.8f);
        }
        TextBox(g, g_pvp[i].desc, fSmall, cSub, RectF(c.X + 62, c.Y + 42, c.Width - 84, 18), false);
        y += 72 + 12;
    }
    return y;
}

static float PageConfig(Graphics& g, float y) {
    RectF r(CX, y, CW, 192);
    CardBg(g, r, ID_CARD + 40);
    Text(g, L"Personalização de Cor", fCardT, cText, r.X + 26, r.Y + 26);
    TextBox(g, L"Escolha uma cor pronta ou clique no quadrado para escolher qualquer cor.", fDesc, cSub, RectF(r.X + 26, r.Y + 55, 380, 40));
    Button(g, RectF(r.X + r.Width - 26 - 120, r.Y + 24, 120, 40), L"Resetar", ID_RESET_COLOR, BTN_GHOST);
    RectF sw(r.X + 26, r.Y + 112, 58, 52);
    AddHit(ID_COLOR_PICK, sw, true);
    float sh = Hov(ID_COLOR_PICK);
    RectF swd = Inflate(sw, sh * 2);
    Glow(g, swd, 6, Acc(), 0.8f + 0.6f * sh, 3);
    LinearGradientBrush swb(PointF(swd.X, swd.Y), PointF(swd.X + swd.Width, swd.Y + swd.Height), Acc(), Mix(Acc(), Acc2(), 0.35f));
    FillRound(g, swd, 6, swb);
    StrokeRound(g, swd, 6, Color((BYTE)(60 + 120 * sh), 255, 255, 255), 1.2f);
    if (sh > 0.05f) {
        Color ic = Luma(Acc()) > 0.6f ? Color((BYTE)(220 * sh), 10, 10, 11) : Color((BYTE)(240 * sh), 255, 255, 255);
        Pen pp(ic, 1.8f);
        pp.SetStartCap(LineCapRound);
        pp.SetEndCap(LineCapRound);
        float mx = swd.X + swd.Width / 2, my = swd.Y + swd.Height / 2;
        g.DrawLine(&pp, mx - 6, my + 6, mx + 5, my - 5);
        g.DrawLine(&pp, mx - 6, my + 6, mx - 7, my + 7);
        SolidBrush tip(ic);
        g.FillEllipse(&tip, mx + 3, my - 8, 6.f, 6.f);
    }
    for (int i = 0; i < kThemeCount; ++i) {
        float cx = sw.X + sw.Width + 40 + i * 42.f, cy = sw.Y + sw.Height / 2;
        AddHit(ID_SWATCH + i, RectF(cx - 17, cy - 17, 34, 34), true);
        float h = Hov(ID_SWATCH + i);
        float rad = 15 + h * 2;
        if (i == g_theme) {
            Pen ring(Color(230, 255, 255, 255), 2.f);
            g.DrawEllipse(&ring, cx - rad - 4, cy - rad - 4, (rad + 4) * 2, (rad + 4) * 2);
        }
        SolidBrush b(kThemes[i].a);
        g.FillEllipse(&b, cx - rad, cy - rad, rad * 2, rad * 2);
    }
    y += 192 + 18;

    r = RectF(CX, y, CW, 104);
    CardBg(g, r, ID_CARD + 41);
    Text(g, L"Versão " + AppVersion(), fCardT, cText, r.X + 26, r.Y + 28);
    TextBox(g, L"O app se atualiza sozinho pelo GitHub sempre que sai uma versão nova.", fDesc, cSub,
            RectF(r.X + 26, r.Y + 57, r.Width - 260, 40));
    Button(g, RectF(r.X + r.Width - 26 - 200, r.Y + (r.Height - 46) / 2, 200, 46), g_updChecking ? L"Verificando..." : L"Verificar atualização",
           ID_UPDATE, BTN_GHOST, !g_updChecking);
    y += 104 + 18;

    r = RectF(CX, y, CW, 104);
    CardBg(g, r, ID_CARD + 42);
    Text(g, L"Restaurar Sistema (Reverter Tudo)", fCardT, cText, r.X + 26, r.Y + 28);
    TextBox(g, L"Desativa todos os módulos, reativa os serviços e volta o display ao padrão do Windows.", fDesc, cSub,
            RectF(r.X + 26, r.Y + 57, r.Width - 230, 40));
    Button(g, RectF(r.X + r.Width - 26 - 150, r.Y + (r.Height - 46) / 2, 150, 46), L"Reverter tudo", ID_REVERT_ALL, BTN_DANGER, !g_busy);
    y += 104 + 18;
    return y;
}

static void DrawToast(Graphics& g) {
    if (g_toastA < 0.01f || g_toast.empty()) return;
    float a = g_toastA;
    float tw = TextW(g, g_toast, fToast) + 54;
    RectF r(CX + (CW - tw) / 2, H - 64 + (1 - a) * 12, tw, 40);
    FillRound(g, Inflate(r, 6), 26, Color((BYTE)(90 * a), 0, 0, 0));
    FillRound(g, r, 20, Alpha(Color(255, 22, 22, 25), a));
    Color kc = g_toastKind == 1 ? cOk : g_toastKind == 2 ? cErr : g_toastKind == 3 ? cWarn : Acc();
    StrokeRound(g, r, 20, Alpha(Mix(cBorder, kc, 0.45f), a));
    SolidBrush d(Alpha(kc, a));
    g.FillEllipse(&d, r.X + 20, r.Y + 16, 8.f, 8.f);
    Text(g, g_toast, fToast, Alpha(cText, a), r.X + 38, r.Y + 11);
}

static void DrawModal(Graphics& g) {
    if (g_modalA < 0.01f) return;
    float a = g_modalA;
    SolidBrush dim(Color((BYTE)(165 * a), 0, 0, 0));
    g.FillRectangle(&dim, 0.f, 0.f, W, H);
    const wchar_t *title = L"", *desc = L"", *ok = L"Confirmar", *cancel = L"Cancelar";
    int kind = BTN_ACCENT;
    switch (g_modalShown) {
    case MODAL_ENABLE_ALL:
        title = L"Ativar todos desta página?";
        desc = L"Todas as opções desta página serão aplicadas. Algumas precisam reiniciar o PC.";
        ok = L"Ativar tudo";
        break;
    case MODAL_DISABLE_ALL:
        title = L"Desativar todos desta página?";
        desc = L"As opções desta página voltam para os padrões do Windows.";
        ok = L"Desativar tudo"; kind = BTN_DANGER;
        break;
    case MODAL_REVERT:
        title = L"Reverter tudo?";
        desc = L"Desativa todos os módulos, reativa os serviços desativados pelo app e volta o display ao padrão.";
        ok = L"Reverter tudo"; kind = BTN_DANGER;
        break;
    case MODAL_REBOOT:
        title = L"Reinicie o PC para finalizar";
        desc = L"Várias otimizações (timer 0.5ms, registros do mouse e teclado, HAGS, QoS, Sensi...) só passam a valer depois de reiniciar o computador. Salve o que estiver aberto antes.";
        ok = L"Reiniciar agora"; cancel = L"Depois";
        break;
    }
    float mw = 460, mh = g_modalShown == MODAL_REBOOT ? 236.f : 214.f;
    float s = 0.94f + 0.06f * a;
    RectF r(W / 2 - mw * s / 2 + SIDE_W / 2, H / 2 - mh * s / 2, mw * s, mh * s);
    Glow(g, r, 16, Acc(), 0.35f * a, 4);
    FillRound(g, r, 16, Alpha(Color(255, 18, 18, 21), a));
    StrokeRound(g, r, 16, Alpha(Mix(cBorder, Acc(), 0.5f), a));
    Text(g, title, fModalT, Alpha(cText, a), r.X + 28, r.Y + 28);
    TextBox(g, desc, fDesc, Alpha(cSub, a), RectF(r.X + 28, r.Y + 64, r.Width - 56, r.Height - 64 - 84));
    if (g_modal) {
        float bw = (r.Width - 56 - 14) / 2;
        Button(g, RectF(r.X + 28, r.Y + r.Height - 28 - 46, bw, 46), cancel, ID_MODAL_CANCEL, BTN_GHOST);
        Button(g, RectF(r.X + 28 + bw + 14, r.Y + r.Height - 28 - 46, bw, 46), ok, ID_MODAL_OK, kind);
    }
}

static HDC g_bufDC = nullptr, g_bgDC = nullptr;
static HBITMAP g_bufBmp = nullptr, g_bgBmp = nullptr;
static int g_bufW = 0, g_bufH = 0;

static HBITMAP CreateDib(HDC hdc, int w, int h) {
    BITMAPINFO bi{};
    bi.bmiHeader.biSize = sizeof bi.bmiHeader;
    bi.bmiHeader.biWidth = w;
    bi.bmiHeader.biHeight = -h;
    bi.bmiHeader.biPlanes = 1;
    bi.bmiHeader.biBitCount = 32;
    void* bits = nullptr;
    return CreateDIBSection(hdc, &bi, DIB_RGB_COLORS, &bits, nullptr, 0);
}

static void EnsureBuffers(HDC hdc, int pw, int ph) {
    if (g_bufDC && pw == g_bufW && ph == g_bufH) return;
    if (g_bufDC) { DeleteDC(g_bufDC); DeleteObject(g_bufBmp); DeleteDC(g_bgDC); DeleteObject(g_bgBmp); }
    g_bufDC = CreateCompatibleDC(hdc);
    g_bufBmp = CreateDib(hdc, pw, ph);
    SelectObject(g_bufDC, g_bufBmp);
    g_bgDC = CreateCompatibleDC(hdc);
    g_bgBmp = CreateDib(hdc, pw, ph);
    SelectObject(g_bgDC, g_bgBmp);
    g_bufW = pw;
    g_bufH = ph;
    Graphics g(g_bgDC);
    g.SetSmoothingMode(SmoothingModeAntiAlias);
    g.ScaleTransform(g_scale, g_scale);
    DrawBackground(g);
}

static const wchar_t* PageTitle(int p) {
    switch (p) {
        case PG_DASH: return L"Central de Otimização";
        case PG_CFG:  return L"Configurações do Painel";
        default:      return kPageNames[p];
    }
}
static const wchar_t* PageSub(int p) {
    switch (p) {
        case PG_FIX:   return L"Configuração avançada de precisão e resposta (Tracking/Flick)";
        case PG_KBD:   return L"Resposta, repetição e atalhos do teclado";
        case PG_FIVEM: return L"Limpeza do cache e boost automático";
        case PG_PVP:   return L"Conexão, polling rate e ajustes de FPS e rede";
        case PG_SVC:   return L"Gerenciamento de serviços do sistema";
        case PG_DISP:  return L"Otimizações de vídeo e monitor";
        case PG_CFG:   return L"Aparência e restauração";
    }
    return nullptr;
}

static void Paint(HDC hdc, int pw, int ph) {
    EnsureBuffers(hdc, pw, ph);
    HDC mem = g_bufDC;
    BitBlt(mem, 0, 0, pw, ph, g_bgDC, 0, 0, SRCCOPY);
    {
        Graphics g(mem);
        g.SetSmoothingMode(SmoothingModeAntiAlias);
        g.SetTextRenderingHint(TextRenderingHintAntiAlias);
        g.SetPixelOffsetMode(PixelOffsetModeHalf);
        g.ScaleTransform(g_scale, g_scale);
        g_hits.clear();

        DrawChrome(g);

        float slide = (1 - g_pageT) * 14;
        Text(g, PageTitle(g_page), fPage, cText, CX, TITLE_Y + slide - 6);
        if (const wchar_t* sub = PageSub(g_page)) PageSubtitle(g, sub, slide - 4);

        if (g_page == PG_DASH) {
            const wchar_t* chip = L"CodeLabs";
            const wchar_t* txt = L"Desenvolvido pela CodeLabs";
            float cw = TextW(g, chip, fSmall) + 18, tw = TextW(g, txt, fSmall);
            RectF br(CX + CW - (cw + tw + 44), TITLE_Y + slide, cw + tw + 44, 32);
            AddHit(ID_SITE_BADGE, br, true);
            float bh = Hov(ID_SITE_BADGE);
            FillRound(g, br, 16, Mix(Color(255, 12, 12, 14), Color(255, 20, 20, 23), bh));
            StrokeRound(g, br, 16, Mix(cBorder, Color(255, 70, 70, 78), bh));
            RectF chr(br.X + 4, br.Y + 4, cw, 24);
            FillRound(g, chr, 12, Color(255, 39, 39, 42));
            Text(g, chip, fSmall, cText, chr.X + 9, chr.Y + 4.5f);
            Text(g, txt, fSmall, Mix(cSub, cText, bh), chr.X + cw + 9, br.Y + 8.5f);
            Pen cp(Mix(cSub, cText, bh), 1.5f);
            float ax = br.X + br.Width - 17, ay = br.Y + 16;
            g.DrawLine(&cp, ax - 2, ay - 4, ax + 2, ay);
            g.DrawLine(&cp, ax + 2, ay, ax - 2, ay + 4);
        }
        if (g_page == PG_FIX || g_page == PG_KBD) {
            const std::vector<Ref>& refs = g_page == PG_FIX ? g_mouseRefs : g_kbdRefs;
            int on = 0;
            for (auto& r : refs) on += RefState(r).on;
            wchar_t buf[48];
            swprintf(buf, 48, L"%d de %d ativos", on, (int)refs.size());
            float bw = 150;
            Button(g, RectF(CX + CW - bw, TITLE_Y + slide - 2, bw, 40), L"Desativar tudo", ID_DISABLE_ALL, BTN_GHOST, !g_busy);
            float cw = TextW(g, buf, fSmall);
            Text(g, buf, fSmall, cMuted, CX + CW - bw - 16 - cw, TITLE_Y + slide + 10);
        }

        g_clipR = RectF(SIDE_W + 1, VIEW_TOP - 8, W - SIDE_W - 1, VIEW_BOT - VIEW_TOP + 8);
        g.SetClip(g_clipR);
        g_clip = true;
        float y0 = VIEW_TOP + 8 - g_scroll[g_page] + slide;
        float y1 = y0;
        switch (g_page) {
            case PG_DASH:  y1 = PageDashboard(g, y0); break;
            case PG_FIX:   y1 = PageMouse(g, y0); break;
            case PG_KBD:   y1 = PageKeyboard(g, y0); break;
            case PG_FIVEM: y1 = PageFiveM(g, y0); break;
            case PG_SVC:   y1 = PageServices(g, y0); break;
            case PG_DISP:  y1 = PageDisplay(g, y0); break;
            case PG_PVP:   y1 = PagePvP(g, y0); break;
            case PG_CFG:   y1 = PageConfig(g, y0); break;
        }
        g_contentH[g_page] = y1 - y0 + 16;
        g_clip = false;

        float maxs = (std::max)(0.f, g_contentH[g_page] - VIEW_H);
        if (maxs > 0) {
            float trackH = VIEW_H - 20;
            float thumbH = (std::max)(40.f, trackH * VIEW_H / g_contentH[g_page]);
            float thumbY = VIEW_TOP + 6 + (trackH - thumbH) * Clampf(g_scroll[g_page] / maxs, 0, 1);
            FillRound(g, RectF(W - 14, thumbY, 3.5f, thumbH), 1.75f, Color(60, 160, 166, 173));
        }
        if (g_pageT < 0.999f) {
            SolidBrush ov(Color((BYTE)(255 * (1 - g_pageT)), cBg.GetR(), cBg.GetG(), cBg.GetB()));
            g.FillRectangle(&ov, g_clipR);
        }
        g.ResetClip();

        DrawToast(g);
        DrawModal(g);
    }
    BitBlt(hdc, 0, 0, pw, ph, mem, 0, 0, SRCCOPY);
}

static void StartAnim() { SetTimer(g_hwnd, TIMER_ANIM, 15, nullptr); }

static void Toast(const std::wstring& s, int kind) {
    g_toast = s;
    g_toastKind = kind;
    g_toastUntil = GetTickCount64() + 3200;
    StartAnim();
}

static void RefreshStates() {
    for (int i = 0; i < g_tweakCount; ++i) g_tw[i].on = g_tweaks[i].isOn();
    for (int i = 0; i < g_quickCount; ++i) g_qk[i].on = g_quick[i].isOn();
    for (int i = 0; i < g_kbdCount; ++i) g_kb[i].on = g_kbd[i].isOn();
    for (int i = 0; i < g_pvpCount; ++i) g_pv[i].on = g_pvp[i].isOn();
}
static void RefreshServices() {
    g_svcState.resize(g_serviceCount);
    for (int i = 0; i < g_serviceCount; ++i) g_svcState[i] = ServiceQuery(i);
}

static void SetTheme(int i) {
    g_theme = i;
    SaveAppDword(L"Theme", (DWORD)i);
    StartAnim();
}

static void UpdateBoostTimer() {
    if (g_fivem.autoBoost) { SetTimer(g_hwnd, TIMER_BOOST, 3000, nullptr); FiveMAutoBoostTick(); }
    else KillTimer(g_hwnd, TIMER_BOOST);
}

struct Job {
    int kind, src, idx;
    bool on;
    const std::vector<Ref>* refs;
    bool before;
    std::wstring msg;
    std::vector<PingTarget> ping;
};

static DWORD WINAPI Worker(LPVOID p) {
    Job* j = (Job*)p;
    bool ok = true;
    switch (j->kind) {
    case JOB_ONE: {
        Ref r{j->src, j->idx, nullptr};
        ok = RefTweak(r).apply(j->on);
        break;
    }
    case JOB_ALL_REFS:
        for (const Ref& r : *j->refs) {
            Tweak& t = RefTweak(r);
            if (t.isOn() != j->on) ok &= t.apply(j->on);
        }
        break;
    case JOB_REVERT:
        for (int i = 0; i < g_tweakCount; ++i) if (g_tweaks[i].isOn()) ok &= g_tweaks[i].apply(false);
        for (int i = 0; i < g_quickCount; ++i) if (g_quick[i].isOn()) ok &= g_quick[i].apply(false);
        for (int i = 0; i < g_kbdCount; ++i) if (g_kbd[i].isOn()) g_kbd[i].apply(false);
        for (int i = 0; i < g_pvpCount; ++i) if (g_pvp[i].isOn()) g_pvp[i].apply(false);
        for (int i = 0; i < g_serviceCount; ++i)
            if (ServiceWasDisabledByApp(i) || ServiceQuery(i) == SVC_DISABLED) ServiceSet(i, true);
        break;
    case JOB_SVC:
        ok = ServiceSet(j->idx, j->on);
        break;
    case JOB_TOOL:
        ok = FiveMTool(j->idx, j->msg);
        break;
    case JOB_PING:
        RunPingTest(j->ping);
        break;
    }
    PostMessageW(g_hwnd, WM_APP_DONE, (WPARAM)j, ok ? 1 : 0);
    return 0;
}

static void RunJob(Job* j) {
    if (g_busy) { delete j; return; }
    g_busy = true;
    g_busyKind = j->kind;
    g_busyIdx = j->kind == JOB_ONE ? j->src * 1000 + j->idx : j->idx;
    if (j->kind == JOB_ONE) {
        Ref r{j->src, j->idx, nullptr};
        j->before = RefState(r).on;
        RefState(r).on = j->on;
    }
    if (j->kind == JOB_ALL_REFS) for (const Ref& r : *j->refs) RefState(r).on = j->on;
    Toast(j->kind == JOB_PING ? L"Testando conexão..." :
          j->kind == JOB_TOOL ? L"Executando..." : L"Aplicando alterações...", 3);
    g_toastUntil = ~0ULL;
    HANDLE h = CreateThread(nullptr, 0, Worker, j, 0, nullptr);
    if (h) CloseHandle(h);
    StartAnim();
}

static const wchar_t* LabelFor(const Ref& r) {
    for (auto& x : g_mouseRefs) if (x.src == r.src && x.idx == r.idx) return x.label;
    for (auto& x : g_kbdRefs) if (x.src == r.src && x.idx == r.idx) return x.label;
    return RefTweak(r).name;
}

static void OnJobDone(Job* j, bool ok) {
    g_busy = false;
    RefreshStates();
    std::wstring msg;
    bool reboot = false;
    switch (j->kind) {
    case JOB_ONE: {
        Ref r{j->src, j->idx, nullptr};
        msg = std::wstring(LabelFor(r)) + (j->on ? L" · Selecionado!" : L" · Removido");
        reboot = RefTweak(r).reboot;
        if (ok && RefState(r).on == j->before) { msg = std::wstring(LabelFor(r)) + L" · já é o padrão do Windows"; reboot = false; }
        break;
    }
    case JOB_ALL_REFS:
        msg = j->on ? L"Todas as opções ativadas" : L"Todas as opções desativadas";
        reboot = true;
        break;
    case JOB_REVERT:
        msg = L"Padrões do Windows restaurados";
        reboot = true;
        g_disp = {50, 6500, 100};
        DisplayApply();
        DisplaySave();
        RefreshServices();
        break;
    case JOB_SVC:
        RefreshServices();
        msg = std::wstring(g_services[j->idx].name) + (j->on ? L" reativado" : L" desativado");
        break;
    case JOB_TOOL:
        msg = j->msg;
        break;
    case JOB_PING:
        g_ping = j->ping;
        msg = L"Teste de conexão concluído";
        break;
    }
    if (!ok && j->kind != JOB_TOOL) msg = L"Falha: " + msg + L" (execute como administrador)";
    else if (ok && reboot) { msg += L"  ·  reinicie o PC"; g_rebootPending = true; }
    Toast(msg, ok ? 1 : 2);
    if (ok && (j->kind == JOB_ALL_REFS || j->kind == JOB_REVERT)) g_modal = g_modalShown = MODAL_REBOOT;
    delete j;
    if (!g_updPendingPath.empty() && !g_modal) InstallPendingUpdate();
}

static void ApplyDisplayNow(bool save) {
    DisplayApply();
    g_lastDispApply = GetTickCount64();
    if (save) DisplaySave();
}

static void SliderChanged(int k, bool final) {
    if (final || GetTickCount64() - g_lastDispApply > 40) ApplyDisplayNow(final);
    StartAnim();
}

static void SliderSet(int k, float mx) {
    const RectF& t = g_sliderTrack[k];
    float f = Clampf((mx - t.X) / t.Width, 0, 1);
    const SliderDef& d = kSliders[k];
    int v = d.lo + (int)roundf(f * (d.hi - d.lo) / d.step) * d.step;
    if (v != *SliderVal(k)) {
        *SliderVal(k) = v;
        SliderChanged(k, false);
    }
}

static void PollStart() {
    RAWINPUTDEVICE rid{0x01, 0x02, 0, g_hwnd};
    if (!RegisterRawInputDevices(&rid, 1, sizeof rid)) { Toast(L"Não foi possível iniciar o teste", 2); return; }
    g_pollTimes.clear();
    g_pollTimes.reserve(40000);
    LARGE_INTEGER c;
    QueryPerformanceCounter(&c);
    g_pollStart = c.QuadPart;
    g_pollActive = true;
    g_pollLive = 0;
    SetTimer(g_hwnd, TIMER_POLL, 100, nullptr);
    StartAnim();
}

static int PollRate(size_t from) {
    if (g_pollTimes.size() < from + 20) return 0;
    LARGE_INTEGER f;
    QueryPerformanceFrequency(&f);
    std::vector<double> dts;
    for (size_t i = from + 1; i < g_pollTimes.size(); ++i) {
        double dt = (g_pollTimes[i] - g_pollTimes[i - 1]) * 1e6 / f.QuadPart;
        if (dt > 50 && dt < 20000) dts.push_back(dt);
    }
    if (dts.size() < 10) return 0;
    std::nth_element(dts.begin(), dts.begin() + dts.size() / 2, dts.end());
    return (int)lround(1e6 / dts[dts.size() / 2]);
}

static void PollTick() {
    LARGE_INTEGER c, f;
    QueryPerformanceCounter(&c);
    QueryPerformanceFrequency(&f);
    double elapsed = (c.QuadPart - g_pollStart) / (double)f.QuadPart;
    size_t from = g_pollTimes.size() > 600 ? g_pollTimes.size() - 600 : 0;
    g_pollLive = PollRate(from);
    if (elapsed >= 5.0) {
        RAWINPUTDEVICE rid{0x01, 0x02, RIDEV_REMOVE, nullptr};
        RegisterRawInputDevices(&rid, 1, sizeof rid);
        KillTimer(g_hwnd, TIMER_POLL);
        g_pollActive = false;
        g_pollHz = PollRate(0);
        g_pollTimes.clear();
        g_pollTimes.shrink_to_fit();
        if (g_pollHz > 0) {
            wchar_t b[64];
            swprintf(b, 64, L"Polling rate medido: %d Hz", g_pollHz);
            Toast(b, 1);
        } else {
            Toast(L"Movimento insuficiente · mova o mouse durante o teste", 3);
        }
    }
    StartAnim();
}

static bool ReadClipboardText(std::wstring& out) {
    if (!OpenClipboard(g_hwnd)) return false;
    HANDLE h = GetClipboardData(CF_UNICODETEXT);
    bool ok = false;
    if (h) {
        const wchar_t* p = (const wchar_t*)GlobalLock(h);
        if (p) { out.assign(p, wcsnlen(p, 512)); ok = true; GlobalUnlock(h); }
    }
    CloseClipboard();
    return ok;
}

static DWORD WINAPI UpdateThread(LPVOID manual) {
    UpdateInfo info;
    if (!CheckForUpdate(info)) {
        PostMessageW(g_hwnd, WM_APP_UPD_NONE, 0, (LPARAM)manual);
        return 0;
    }
    PostMessageW(g_hwnd, WM_APP_UPD_FOUND, (WPARAM) new std::wstring(info.version), 0);
    std::wstring path;
    if (DownloadUpdate(info, path)) PostMessageW(g_hwnd, WM_APP_UPD_READY, (WPARAM) new std::wstring(path), 0);
    else PostMessageW(g_hwnd, WM_APP_UPD_NONE, 1, (LPARAM)1);
    return 0;
}

static void StartUpdateCheck(bool manual) {
    if (g_updChecking) return;
    g_updChecking = true;
    HANDLE h = CreateThread(nullptr, 0, UpdateThread, (LPVOID)(INT_PTR)(manual ? 1 : 0), 0, nullptr);
    if (h) CloseHandle(h); else g_updChecking = false;
    StartAnim();
}

static void InstallPendingUpdate() {
    if (g_updPendingPath.empty() || g_busy) return;
    std::wstring path = g_updPendingPath;
    g_updPendingPath.clear();
    if (g_mutex) { CloseHandle(g_mutex); g_mutex = nullptr; }
    if (InstallUpdateAndRestart(path)) {
        DestroyWindow(g_hwnd);
    } else {
        g_mutex = CreateMutexW(nullptr, TRUE, L"PrecisionFix.SingleInstance");
        Toast(L"Não foi possível instalar a atualização", 2);
    }
}

static void OnClick(int id) {
    if (id == ID_CLOSE) { PostMessageW(g_hwnd, WM_CLOSE, 0, 0); return; }
    if (id == ID_REBOOT_PILL) { g_modal = g_modalShown = MODAL_REBOOT; return; }
    if (id == ID_UPDATE) { StartUpdateCheck(true); return; }
    if (id == ID_MIN) { ShowWindow(g_hwnd, SW_MINIMIZE); return; }
    if (id >= ID_NAV && id < ID_NAV + PG_COUNT) {
        int p = id - ID_NAV;
        if (g_page != p) { g_page = p; g_pageT = 0; }
        if (p == PG_SVC) RefreshServices();
        return;
    }
    if (id >= ID_SWATCH && id < ID_SWATCH + kThemeCount) { SetTheme(id - ID_SWATCH); return; }
    if (id == ID_COLOR_PICK) {
        static COLORREF custom[16] = {};
        Theme cur = CurrentTheme();
        CHOOSECOLORW cc{};
        cc.lStructSize = sizeof cc;
        cc.hwndOwner = g_hwnd;
        cc.rgbResult = RGB(cur.a.GetR(), cur.a.GetG(), cur.a.GetB());
        cc.lpCustColors = custom;
        cc.Flags = CC_FULLOPEN | CC_RGBINIT | CC_ANYCOLOR;
        g_mx = g_my = -1;
        if (ChooseColorW(&cc)) {
            g_customColor = (GetRValue(cc.rgbResult) << 16) | (GetGValue(cc.rgbResult) << 8) | GetBValue(cc.rgbResult);
            SaveAppDword(L"ThemeCustom", g_customColor);
            SetTheme(kThemeCount);
            Toast(L"Cor personalizada aplicada", 1);
        }
        return;
    }
    if (id == ID_RESET_COLOR) { SetTheme(0); Toast(L"Cores originais restauradas", 1); return; }
    if (id == ID_SITE || id == ID_SITE_SIDE || id == ID_SITE_BADGE) {
        ShellExecuteW(nullptr, L"open", L"explorer.exe", kSiteUrl, nullptr, SW_SHOWNORMAL);
        return;
    }
    if (id == ID_MODAL_CANCEL) {
        g_modal = MODAL_NONE;
        if (!g_updPendingPath.empty()) InstallPendingUpdate();
        return;
    }
    if (id == ID_MODAL_OK) {
        int m = g_modal;
        g_modal = MODAL_NONE;
        if (m == MODAL_REBOOT) {
            std::wstring cmd = L"\"" + std::wstring(_wgetenv(L"SystemRoot") ? _wgetenv(L"SystemRoot") : L"C:\\Windows") +
                               L"\\System32\\shutdown.exe\" /r /t 10 /c \"Precision Fix: reiniciando para aplicar as otimizacoes\"";
            STARTUPINFOW si{};
            si.cb = sizeof si;
            si.dwFlags = STARTF_USESHOWWINDOW;
            si.wShowWindow = SW_HIDE;
            PROCESS_INFORMATION pi{};
            if (CreateProcessW(nullptr, &cmd[0], nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi)) {
                CloseHandle(pi.hThread);
                CloseHandle(pi.hProcess);
                Toast(L"O PC vai reiniciar em 10 segundos...", 3);
            } else {
                Toast(L"Não foi possível reiniciar · reinicie manualmente", 2);
            }
            return;
        }
        if (m == MODAL_REVERT) RunJob(new Job{JOB_REVERT, 0, -1, false, nullptr, false, L""});
        else {
            const std::vector<Ref>* refs = g_page == PG_KBD ? &g_kbdRefs : &g_mouseRefs;
            RunJob(new Job{JOB_ALL_REFS, 0, -1, m == MODAL_ENABLE_ALL, refs, false, L""});
        }
        return;
    }
    if (id >= ID_PRESET && id < ID_PRESET + kPresetCount) {
        const Preset& p = kPresets[id - ID_PRESET];
        g_disp = {p.sat, p.temp, p.gam};
        ApplyDisplayNow(true);
        Toast(std::wstring(L"Preset ") + p.name + L" aplicado", 1);
        return;
    }
    if (id >= ID_STEP && id < ID_STEP + 6) {
        int k = (id - ID_STEP) / 2, dir = (id - ID_STEP) % 2 ? 1 : -1;
        int v = *SliderVal(k) + dir * kSliders[k].step;
        *SliderVal(k) = (std::max)(kSliders[k].lo, (std::min)(kSliders[k].hi, v));
        SliderChanged(k, true);
        return;
    }
    if (id == ID_POLL && !g_pollActive) { PollStart(); return; }
    if (id == ID_PASTE) {
        std::wstring clip, host;
        int port;
        if (!ReadClipboardText(clip)) { Toast(L"Área de transferência vazia", 3); return; }
        if (!ParseServerAddr(clip, host, port)) {
            Toast(clip.find(L"cfx.re") != std::wstring::npos ? L"Use o IP:porta do servidor (link cfx.re não tem IP)"
                                                               : L"Copie o IP do servidor, ex: 123.45.67.89:30120", 3);
            return;
        }
        wchar_t b[300];
        swprintf(b, 300, L"%ls:%d", host.c_str(), port);
        SaveServerAddr(b);
        g_ping.clear();
        Toast(std::wstring(L"Servidor salvo: ") + b, 1);
        return;
    }
    if (id == ID_FMOPT) {
        g_fivem.autoBoost = !g_fivem.autoBoost;
        FiveMSaveOpts();
        UpdateBoostTimer();
        Toast(g_fivem.autoBoost ? L"BOOST AUTOMÁTICO · Selecionado!" : L"BOOST AUTOMÁTICO · Removido", 1);
        return;
    }
    if (g_busy) return;
    if (id >= ID_MCHECK && id < ID_MCHECK + (int)g_mouseRefs.size()) {
        const Ref& r = g_mouseRefs[id - ID_MCHECK];
        RunJob(new Job{JOB_ONE, r.src, r.idx, !RefState(r).on, nullptr, false, L""});
        return;
    }
    if (id >= ID_KCHECK && id < ID_KCHECK + (int)g_kbdRefs.size()) {
        const Ref& r = g_kbdRefs[id - ID_KCHECK];
        RunJob(new Job{JOB_ONE, r.src, r.idx, !RefState(r).on, nullptr, false, L""});
        return;
    }
    if (id >= ID_PVPOPT && id < ID_PVPOPT + g_pvpCount) {
        int i = id - ID_PVPOPT;
        RunJob(new Job{JOB_ONE, SRC_PVP, i, !g_pv[i].on, nullptr, false, L""});
        return;
    }
    if (id >= ID_QUICK && id < ID_QUICK + g_quickCount) {
        int i = id - ID_QUICK;
        RunJob(new Job{JOB_ONE, SRC_QUICK, i, !g_qk[i].on, nullptr, false, L""});
        return;
    }
    if (id >= ID_SVC && id < ID_SVC + g_serviceCount) {
        int i = id - ID_SVC;
        if (g_svcState[i] == SVC_RUNNING) Toast(std::wstring(g_services[i].name) + L" está ativo · não é possível desativar", 3);
        else RunJob(new Job{JOB_SVC, 0, i, true, nullptr, false, L""});
        return;
    }
    if (id >= ID_TOOL && id < ID_TOOL + TOOL_COUNT) { RunJob(new Job{JOB_TOOL, 0, id - ID_TOOL, true, nullptr, false, L""}); return; }
    if (id == ID_PING) {
        Job* j = new Job{JOB_PING, 0, -1, true, nullptr, false, L""};
        j->ping = PingTargets();
        g_ping.clear();
        RunJob(j);
        return;
    }
    if (id == ID_DISABLE_ALL) { g_modal = g_modalShown = MODAL_DISABLE_ALL; return; }
    if (id == ID_REVERT_ALL)  { g_modal = g_modalShown = MODAL_REVERT; return; }
}

static bool TickAnim() {
    bool moving = false;
    auto approach = [&](float& v, float target, float k) {
        float d = target - v;
        if (fabsf(d) < 0.002f) { v = target; return; }
        v += d * k;
        moving = true;
    };
    for (auto* vec : {&g_tw, &g_qk, &g_kb, &g_pv, &g_fm})
        for (auto& t : *vec) approach(t.anim, t.on ? 1.f : 0.f, 0.22f);
    for (auto& kv : g_hov) approach(kv.second, MouseIn(kv.first) ? 1.f : 0.f, 0.24f);

    Theme th = CurrentTheme();
    float ta[3] = {(float)th.a.GetR(), (float)th.a.GetG(), (float)th.a.GetB()};
    float tb[3] = {(float)th.b.GetR(), (float)th.b.GetG(), (float)th.b.GetB()};
    for (int i = 0; i < 3; ++i) {
        if (fabsf(ta[i] - g_acc[i]) > 0.5f) { g_acc[i] += (ta[i] - g_acc[i]) * 0.2f; moving = true; } else g_acc[i] = ta[i];
        if (fabsf(tb[i] - g_acc2[i]) > 0.5f) { g_acc2[i] += (tb[i] - g_acc2[i]) * 0.2f; moving = true; } else g_acc2[i] = tb[i];
    }

    approach(g_pageT, 1.f, 0.2f);
    for (int p = 0; p < PG_COUNT; ++p) {
        float maxs = (std::max)(0.f, g_contentH[p] - VIEW_H);
        g_scrollT[p] = Clampf(g_scrollT[p], 0, maxs);
        float before = g_scroll[p];
        approach(g_scroll[p], g_scrollT[p], 0.22f);
        if (fabsf(before - g_scroll[p]) > 0.01f) moving = true;
    }
    bool toastOn = GetTickCount64() < g_toastUntil;
    approach(g_toastA, toastOn ? 1.f : 0.f, 0.2f);
    if (toastOn) moving = true;
    approach(g_modalA, g_modal ? 1.f : 0.f, 0.25f);
    if (g_busy) { g_pulse += 0.15f; moving = true; }
    return moving;
}

static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
    case WM_NCHITTEST: {
        POINT pt = {GET_X_LPARAM(lp), GET_Y_LPARAM(lp)};
        ScreenToClient(hwnd, &pt);
        float x = pt.x / g_scale, y = pt.y / g_scale;
        if (y < TOP_H - 6 && !g_modal) {
            for (auto& h : g_hits)
                if (h.click && h.r.Contains(x, y)) return HTCLIENT;
            return HTCAPTION;
        }
        return HTCLIENT;
    }
    case WM_SETCURSOR:
        if (LOWORD(lp) == HTCLIENT) {
            SetCursor(LoadCursor(nullptr, (TopClick() || g_drag >= 0) ? IDC_HAND : IDC_ARROW));
            return TRUE;
        }
        break;
    case WM_MOUSEMOVE:
        if (!g_tracking) {
            TRACKMOUSEEVENT tme{sizeof tme, TME_LEAVE, hwnd, 0};
            TrackMouseEvent(&tme);
            g_tracking = true;
        }
        g_mx = GET_X_LPARAM(lp) / g_scale;
        g_my = GET_Y_LPARAM(lp) / g_scale;
        if (g_drag >= 0) { SliderSet(g_drag, g_mx); return 0; }
        {
            static unsigned long long lastSig = 0;
            unsigned long long sig = 1469598103934665603ULL;
            for (auto& h : g_hits)
                if (h.r.Contains(g_mx, g_my)) sig = (sig ^ (unsigned)h.id) * 1099511628211ULL;
            if (sig != lastSig) { lastSig = sig; StartAnim(); }
        }
        return 0;
    case WM_MOUSELEAVE:
        g_tracking = false;
        if (g_drag < 0) g_mx = g_my = -1;
        StartAnim();
        return 0;
    case WM_LBUTTONDOWN:
        g_mx = GET_X_LPARAM(lp) / g_scale;
        g_my = GET_Y_LPARAM(lp) / g_scale;
        g_pressId = TopClick();
        SetCapture(hwnd);
        if (g_pressId >= ID_SLIDER && g_pressId < ID_SLIDER + 3) {
            g_drag = g_pressId - ID_SLIDER;
            SliderSet(g_drag, g_mx);
        }
        return 0;
    case WM_LBUTTONUP: {
        ReleaseCapture();
        g_mx = GET_X_LPARAM(lp) / g_scale;
        g_my = GET_Y_LPARAM(lp) / g_scale;
        if (g_drag >= 0) {
            int k = g_drag;
            g_drag = -1;
            SliderChanged(k, true);
            g_pressId = 0;
            StartAnim();
            return 0;
        }
        int id = TopClick();
        if (id && id == g_pressId) OnClick(id);
        g_pressId = 0;
        StartAnim();
        return 0;
    }
    case WM_INPUT:
        if (g_pollActive) {
            RAWINPUT ri;
            UINT sz = sizeof ri;
            if (GetRawInputData((HRAWINPUT)lp, RID_INPUT, &ri, &sz, sizeof(RAWINPUTHEADER)) != (UINT)-1 &&
                ri.header.dwType == RIM_TYPEMOUSE && (ri.data.mouse.lLastX || ri.data.mouse.lLastY)) {
                LARGE_INTEGER c;
                QueryPerformanceCounter(&c);
                if (g_pollTimes.size() < 60000) g_pollTimes.push_back(c.QuadPart);
            }
        }
        break;
    case WM_KEYDOWN:
        if (wp == VK_ESCAPE && g_modal) { g_modal = MODAL_NONE; StartAnim(); }
        return 0;
    case WM_MOUSEWHEEL:
        if (!g_modal) {
            float maxs = (std::max)(0.f, g_contentH[g_page] - VIEW_H);
            g_scrollT[g_page] = Clampf(g_scrollT[g_page] - GET_WHEEL_DELTA_WPARAM(wp) / 120.f * 80.f, 0, maxs);
            StartAnim();
        }
        return 0;
    case WM_TIMER:
        if (wp == TIMER_ANIM) {
            if (!TickAnim()) KillTimer(hwnd, TIMER_ANIM);
            InvalidateRect(hwnd, nullptr, FALSE);
        } else if (wp == TIMER_POLL) {
            PollTick();
        } else if (wp == TIMER_BOOST) {
            FiveMAutoBoostTick();
        }
        return 0;
    case WM_APP_UPD_FOUND: {
        std::wstring* v = (std::wstring*)wp;
        Toast(L"Nova versão " + *v + L" encontrada · baixando...", 3);
        g_toastUntil = ~0ULL;
        delete v;
        return 0;
    }
    case WM_APP_UPD_READY: {
        std::wstring* p = (std::wstring*)wp;
        g_updPendingPath = *p;
        delete p;
        g_updChecking = false;
        Toast(L"Atualização baixada · reiniciando o app...", 1);
        if (!g_busy && !g_modal) InstallPendingUpdate();
        return 0;
    }
    case WM_APP_UPD_NONE:
        g_updChecking = false;
        if (lp) Toast(wp ? L"Não foi possível verificar/baixar a atualização" : L"Você já está na versão mais recente", wp ? 2 : 1);
        StartAnim();
        return 0;
    case WM_APP_DONE:
        OnJobDone((Job*)wp, lp != 0);
        StartAnim();
        return 0;
    case WM_ERASEBKGND:
        return 1;
    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(hwnd, &ps);
        RECT rc;
        GetClientRect(hwnd, &rc);
        Paint(hdc, rc.right, rc.bottom);
        EndPaint(hwnd, &ps);
        return 0;
    }
    case WM_CLOSE:
        if (g_busy) { Toast(L"Aguarde terminar de aplicar...", 3); return 0; }
        DestroyWindow(hwnd);
        return 0;
    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

static void InitState() {
    g_admin = IsElevated();
    g_tw.resize(g_tweakCount);
    g_qk.resize(g_quickCount);
    g_kb.resize(g_kbdCount);
    g_pv.resize(g_pvpCount);
    BuildRefs();
    RefreshStates();
    for (auto* vec : {&g_tw, &g_qk, &g_kb, &g_pv})
        for (auto& t : *vec) t.anim = t.on ? 1.f : 0.f;
    FiveMLoadOpts();
    g_fm[0].on = g_fivem.autoBoost;
    g_fm[0].anim = g_fivem.autoBoost ? 1.f : 0.f;
    DisplayLoad();
    g_theme = (int)LoadAppDword(L"Theme", 0);
    g_customColor = LoadAppDword(L"ThemeCustom", 0xA855F7);
    if (g_theme < 0 || g_theme > kThemeCount) g_theme = 0;
    Theme th = CurrentTheme();
    g_acc[0] = th.a.GetR(); g_acc[1] = th.a.GetG(); g_acc[2] = th.a.GetB();
    g_acc2[0] = th.b.GetR(); g_acc2[1] = th.b.GetG(); g_acc2[2] = th.b.GetB();
    SysInfoInit();
}

#ifndef PF_NO_WINMAIN
int WINAPI wWinMain(HINSTANCE hInst, HINSTANCE, PWSTR, int nShow) {
    bool justUpdated = wcsstr(GetCommandLineW(), L"--updated") != nullptr;
    if (justUpdated) Sleep(800);
    CleanupOldVersion();
    g_mutex = CreateMutexW(nullptr, TRUE, L"PrecisionFix.SingleInstance");
    if (GetLastError() == ERROR_ALREADY_EXISTS) {
        HWND other = FindWindowW(L"PrecisionFixWnd", nullptr);
        if (other) { ShowWindow(other, SW_RESTORE); SetForegroundWindow(other); }
        return 0;
    }

    g_inst = hInst;
    GdiplusStartupInput gsi;
    ULONG_PTR gtok;
    GdiplusStartup(&gtok, &gsi, nullptr);

    HDC sdc = GetDC(nullptr);
    g_scale = GetDeviceCaps(sdc, LOGPIXELSY) / 96.f;
    ReleaseDC(nullptr, sdc);

    CreateFonts();
    InitState();
    for (int i = 0; i < g_tweakCount; ++i)
        if (IsTimerTweak(i) && g_tw[i].on) SetRuntimeTimerResolution(true);

    WNDCLASSEXW wc{};
    wc.cbSize = sizeof wc;
    wc.style = CS_DROPSHADOW;
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hInst;
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wc.hIcon = LoadIconW(hInst, MAKEINTRESOURCEW(1));
    wc.hIconSm = (HICON)LoadImageW(hInst, MAKEINTRESOURCEW(1), IMAGE_ICON, 16, 16, 0);
    wc.lpszClassName = L"PrecisionFixWnd";
    RegisterClassExW(&wc);

    int ww = (int)(W * g_scale + 0.5f), wh = (int)(H * g_scale + 0.5f);
    RECT wa;
    SystemParametersInfoW(SPI_GETWORKAREA, 0, &wa, 0);
    int x = wa.left + (wa.right - wa.left - ww) / 2;
    int y = wa.top + (wa.bottom - wa.top - wh) / 2;

    g_hwnd = CreateWindowExW(0, wc.lpszClassName, L"Precision Fix", WS_POPUP | WS_MINIMIZEBOX | WS_SYSMENU,
                             x, y, ww, wh, nullptr, nullptr, hInst, nullptr);

    DWORD corner = 2;
    DwmSetWindowAttribute(g_hwnd, 33, &corner, sizeof corner);
    COLORREF border = RGB(30, 33, 38);
    DwmSetWindowAttribute(g_hwnd, 34, &border, sizeof border);
    BOOL dark = TRUE;
    DwmSetWindowAttribute(g_hwnd, 20, &dark, sizeof dark);

    ShowWindow(g_hwnd, nShow);
    UpdateWindow(g_hwnd);

    if (!DisplayIsNeutral()) DisplayApply();
    UpdateBoostTimer();

    if (justUpdated) Toast(L"Precision Fix atualizado para a versão " + AppVersion(), 1);
    StartUpdateCheck(false);

    if (LoadAppDword(L"FirstRunDone", 0) == 0) {
        SaveAppDword(L"FirstRunDone", 1);
        ShellExecuteW(nullptr, L"open", L"explorer.exe", kSiteUrl, nullptr, SW_SHOWNORMAL);
    }

    MSG m;
    while (GetMessageW(&m, nullptr, 0, 0) > 0) {
        TranslateMessage(&m);
        DispatchMessageW(&m);
    }

    DisplayShutdown();
    SetRuntimeTimerResolution(false);
    GdiplusShutdown(gtok);
    if (g_mutex) CloseHandle(g_mutex);
    return 0;
}
#endif

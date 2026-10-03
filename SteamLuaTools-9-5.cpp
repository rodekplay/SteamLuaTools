// ============================================================
//  SteamLuaTools 9.5 — MiniGames + Level + CatMode
// ============================================================
#pragma execution_character_set("utf-8")
#pragma warning(disable: 4061)
#pragma warning(disable: 4062)

#ifndef WINVER
#define WINVER 0x0A00
#endif
#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0A00
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <windows.h>
#include <dwmapi.h>
#include <winhttp.h>
#include <d3d11.h>
#include <dxgi.h>
#include <shellapi.h>
#include <shlobj.h>
#include <commdlg.h>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>
#include <deque>
#include <map>
#include <set>
#include <unordered_map>
#include <unordered_set>
#include <thread>
#include <mutex>
#include <atomic>
#include <chrono>
#include <cmath>
#include <random>
#include <algorithm>

#include "imgui.h"
#include "imgui_impl_win32.h"
#include "imgui_impl_dx11.h"
#include "resource.h"
#include "stb_image.h"
#include "json.hpp"

#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "dxgi.lib")
#pragma comment(lib, "dwmapi.lib")
#pragma comment(lib, "winhttp.lib")
#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "comdlg32.lib")

namespace fs = std::filesystem;
using json = nlohmann::json;

#ifndef MY_CLAMP
#define MY_CLAMP(v, mn, mx)  (((v) < (mn)) ? (mn) : (((v) > (mx)) ? (mx) : (v)))
#endif

// ============================================================
//  D3D11
// ============================================================
static ID3D11Device* g_pd3dDevice = nullptr;
static ID3D11DeviceContext* g_pd3dDeviceContext = nullptr;
static IDXGISwapChain* g_pSwapChain = nullptr;
static UINT g_ResizeWidth = 0, g_ResizeHeight = 0;
static ID3D11RenderTargetView* g_mainRenderTargetView = nullptr;

static bool CreateDeviceD3D(HWND hWnd);
static void CleanupDeviceD3D();
static void CreateRenderTarget();
static void CleanupRenderTarget();
static LRESULT WINAPI WndProc(HWND, UINT, WPARAM, LPARAM);
static void LogPush(const std::string& text, bool is_error = false);
static void SaveSettings();
static void PushAchievementToast(const std::string& t);
static void PushToast(const std::string& t);

static HWND g_hwnd = nullptr;
static std::string g_app_steam_path;

// ============================================================
//  Утилиты
// ============================================================
static float ExpLerp(float a, float b, float s, float dt) { return a + (b - a) * (1.0f - std::exp(-s * dt)); }
static ImVec2 ExpLerp2(ImVec2 a, ImVec2 b, float s, float dt) {
    return ImVec2(ExpLerp(a.x, b.x, s, dt), ExpLerp(a.y, b.y, s, dt));
}
static float EaseOutCubic(float t) { float p = 1.0f - t; return 1.0f - p * p * p; }

static void CopyToClipboard(const std::string& text) {
    if (text.empty()) return;
    if (!OpenClipboard(g_hwnd)) return;
    EmptyClipboard();
    size_t sz = text.size() + 1;
    HGLOBAL h = GlobalAlloc(GMEM_MOVEABLE, sz);
    if (h) { memcpy(GlobalLock(h), text.c_str(), sz); GlobalUnlock(h); SetClipboardData(CF_TEXT, h); }
    CloseClipboard();
}

// ============================================================
//  Шрифты
// ============================================================
static ImFont* g_font_regular = nullptr;
static ImFont* g_font_big = nullptr;
static ImFont* g_font_title = nullptr;
static ImFont* g_font_small = nullptr;

static void LoadFonts() {
    ImGuiIO& io = ImGui::GetIO();
    const char* candidates[] = {
        "C:\\Windows\\Fonts\\segoeui.ttf",
        "C:\\Windows\\Fonts\\tahoma.ttf",
        "C:\\Windows\\Fonts\\arial.ttf",
    };
    ImFontConfig cfg;
    cfg.OversampleH = 3; cfg.OversampleV = 3; cfg.PixelSnapH = true;
    static const ImWchar ranges[] = {
        0x0020,0x00FF, 0x0100,0x017F, 0x0180,0x024F,
        0x0400,0x04FF, 0x0500,0x052F,
        0x2000,0x206F, 0x2070,0x209F, 0x20A0,0x20CF,
        0x2100,0x214F, 0x2150,0x218F, 0x2190,0x21FF,
        0x2200,0x22FF, 0x2300,0x23FF, 0x2500,0x257F,
        0x25A0,0x25FF, 0x2600,0x26FF, 0x2700,0x27BF,
        0,
    };
    for (auto path : candidates) {
        if (fs::exists(path)) {
            g_font_regular = io.Fonts->AddFontFromFileTTF(path, 16.0f, &cfg, ranges);
            g_font_small = io.Fonts->AddFontFromFileTTF(path, 13.0f, &cfg, ranges);
            g_font_big = io.Fonts->AddFontFromFileTTF(path, 24.0f, &cfg, ranges);
            g_font_title = io.Fonts->AddFontFromFileTTF(path, 21.0f, &cfg, ranges);
            if (g_font_regular) { io.FontDefault = g_font_regular; return; }
        }
    }
    io.Fonts->AddFontDefault();
}

static void ApplyAppIcon(HWND hwnd) {
    HINSTANCE hInst = GetModuleHandle(nullptr);
    HICON hBig = (HICON)LoadImageW(hInst, MAKEINTRESOURCEW(IDI_ICON1), IMAGE_ICON,
        GetSystemMetrics(SM_CXICON), GetSystemMetrics(SM_CYICON), 0);
    HICON hSmall = (HICON)LoadImageW(hInst, MAKEINTRESOURCEW(IDI_ICON1), IMAGE_ICON,
        GetSystemMetrics(SM_CXSMICON), GetSystemMetrics(SM_CYSMICON), 0);
    if (hBig) SendMessageW(hwnd, WM_SETICON, ICON_BIG, (LPARAM)hBig);
    if (hSmall) SendMessageW(hwnd, WM_SETICON, ICON_SMALL, (LPARAM)hSmall);
}

static std::string GetSettingsPath() {
    char* appdata = nullptr; size_t len = 0;
    _dupenv_s(&appdata, &len, "APPDATA");
    std::string base;
    if (appdata) { base = appdata; free(appdata); }
    else base = ".";
    fs::path dir = fs::path(base) / "SteamLuaTools";
    std::error_code ec; fs::create_directories(dir, ec);
    return (dir / "settings.json").string();
}

// ============================================================
//  Темы / FX
// ============================================================
enum class ThemeMode { Dark = 0, Blue, Purple, Green, Red, Orange, Pink, Teal, Yellow, Gray, Sunset, Ocean, Forest, Custom };
static ThemeMode g_theme = ThemeMode::Dark;

enum class FxMode { Aurora = 0, Waves, Particles, Gradient, Grid, Count };
static FxMode g_fx = FxMode::Aurora;
static bool g_fx_snow = false;
static bool g_fx_rain = false;
static bool g_fx_leaves = false;

static float g_custom_color_a[4] = { 0.627f, 0.420f, 1.000f, 1.0f };
static float g_custom_color_b[4] = { 0.980f, 0.400f, 0.650f, 1.0f };

struct ThemeColors {
    ImVec4 window_bg, topbar_bg;
    ImVec4 accent, accent2, accent3;
    ImVec4 accent_hover, accent_active;
    ImVec4 card_bg, card_bg_hover;
    ImVec4 text, text_disabled;
};

static ThemeColors GetTheme() {
    ThemeColors t{};
    t.text = ImVec4(0.925f, 0.915f, 0.960f, 1.0f);
    t.text_disabled = ImVec4(0.502f, 0.478f, 0.588f, 1.0f);
    t.card_bg = ImVec4(1, 1, 1, 0.035f);
    t.card_bg_hover = ImVec4(1, 1, 1, 0.07f);
    switch (g_theme) {
    case ThemeMode::Blue:
        t.window_bg = ImVec4(0.024f, 0.035f, 0.078f, 1); t.topbar_bg = t.window_bg;
        t.accent = ImVec4(0.35f, 0.60f, 1.00f, 1); t.accent2 = ImVec4(0.60f, 0.80f, 1.00f, 1);
        t.accent3 = ImVec4(0.20f, 0.45f, 0.90f, 1); t.accent_hover = ImVec4(0.45f, 0.70f, 1.00f, 1);
        t.accent_active = ImVec4(0.25f, 0.50f, 0.90f, 1); break;
    case ThemeMode::Purple:
        t.window_bg = ImVec4(0.039f, 0.027f, 0.063f, 1); t.topbar_bg = t.window_bg;
        t.accent = ImVec4(0.627f, 0.420f, 1.000f, 1); t.accent2 = ImVec4(0.980f, 0.400f, 0.650f, 1);
        t.accent3 = ImVec4(0.380f, 0.700f, 1.000f, 1); t.accent_hover = ImVec4(0.72f, 0.53f, 1.00f, 1);
        t.accent_active = ImVec4(0.52f, 0.33f, 0.90f, 1); break;
    case ThemeMode::Green:
        t.window_bg = ImVec4(0.024f, 0.063f, 0.039f, 1); t.topbar_bg = t.window_bg;
        t.accent = ImVec4(0.30f, 0.85f, 0.50f, 1); t.accent2 = ImVec4(0.60f, 1.00f, 0.70f, 1);
        t.accent3 = ImVec4(0.15f, 0.60f, 0.35f, 1); t.accent_hover = ImVec4(0.40f, 0.95f, 0.60f, 1);
        t.accent_active = ImVec4(0.20f, 0.70f, 0.40f, 1); break;
    case ThemeMode::Red:
        t.window_bg = ImVec4(0.063f, 0.024f, 0.035f, 1); t.topbar_bg = t.window_bg;
        t.accent = ImVec4(1.00f, 0.35f, 0.40f, 1); t.accent2 = ImVec4(1.00f, 0.60f, 0.60f, 1);
        t.accent3 = ImVec4(0.80f, 0.20f, 0.30f, 1); t.accent_hover = ImVec4(1.00f, 0.45f, 0.50f, 1);
        t.accent_active = ImVec4(0.85f, 0.25f, 0.30f, 1); break;
    case ThemeMode::Orange:
        t.window_bg = ImVec4(0.063f, 0.039f, 0.016f, 1); t.topbar_bg = t.window_bg;
        t.accent = ImVec4(1.00f, 0.60f, 0.20f, 1); t.accent2 = ImVec4(1.00f, 0.80f, 0.50f, 1);
        t.accent3 = ImVec4(0.85f, 0.45f, 0.10f, 1); t.accent_hover = ImVec4(1.00f, 0.70f, 0.30f, 1);
        t.accent_active = ImVec4(0.85f, 0.50f, 0.15f, 1); break;
    case ThemeMode::Pink:
        t.window_bg = ImVec4(0.078f, 0.027f, 0.063f, 1); t.topbar_bg = t.window_bg;
        t.accent = ImVec4(1.00f, 0.45f, 0.75f, 1); t.accent2 = ImVec4(1.00f, 0.70f, 0.85f, 1);
        t.accent3 = ImVec4(0.85f, 0.35f, 0.65f, 1); t.accent_hover = ImVec4(1.00f, 0.55f, 0.85f, 1);
        t.accent_active = ImVec4(0.85f, 0.35f, 0.65f, 1); break;
    case ThemeMode::Teal:
        t.window_bg = ImVec4(0.016f, 0.063f, 0.063f, 1); t.topbar_bg = t.window_bg;
        t.accent = ImVec4(0.20f, 0.90f, 0.90f, 1); t.accent2 = ImVec4(0.60f, 1.00f, 1.00f, 1);
        t.accent3 = ImVec4(0.15f, 0.70f, 0.70f, 1); t.accent_hover = ImVec4(0.30f, 1.00f, 1.00f, 1);
        t.accent_active = ImVec4(0.15f, 0.75f, 0.75f, 1); break;
    case ThemeMode::Yellow:
        t.window_bg = ImVec4(0.063f, 0.063f, 0.016f, 1); t.topbar_bg = t.window_bg;
        t.accent = ImVec4(0.95f, 0.90f, 0.30f, 1); t.accent2 = ImVec4(1.00f, 0.95f, 0.60f, 1);
        t.accent3 = ImVec4(0.80f, 0.75f, 0.20f, 1); t.accent_hover = ImVec4(1.00f, 0.95f, 0.40f, 1);
        t.accent_active = ImVec4(0.80f, 0.75f, 0.20f, 1); break;
    case ThemeMode::Gray:
        t.window_bg = ImVec4(0.055f, 0.055f, 0.055f, 1); t.topbar_bg = t.window_bg;
        t.accent = ImVec4(0.65f, 0.65f, 0.65f, 1); t.accent2 = ImVec4(0.85f, 0.85f, 0.85f, 1);
        t.accent3 = ImVec4(0.45f, 0.45f, 0.45f, 1); t.accent_hover = ImVec4(0.80f, 0.80f, 0.80f, 1);
        t.accent_active = ImVec4(0.55f, 0.55f, 0.55f, 1); break;
    case ThemeMode::Sunset:
        t.window_bg = ImVec4(0.078f, 0.027f, 0.043f, 1); t.topbar_bg = t.window_bg;
        t.accent = ImVec4(1.00f, 0.55f, 0.25f, 1); t.accent2 = ImVec4(1.00f, 0.40f, 0.55f, 1);
        t.accent3 = ImVec4(0.85f, 0.40f, 0.15f, 1); t.accent_hover = ImVec4(1.00f, 0.35f, 0.55f, 1);
        t.accent_active = ImVec4(0.85f, 0.40f, 0.15f, 1); break;
    case ThemeMode::Ocean:
        t.window_bg = ImVec4(0.016f, 0.039f, 0.078f, 1); t.topbar_bg = t.window_bg;
        t.accent = ImVec4(0.20f, 0.60f, 0.95f, 1); t.accent2 = ImVec4(0.40f, 0.80f, 1.00f, 1);
        t.accent3 = ImVec4(0.15f, 0.45f, 0.85f, 1); t.accent_hover = ImVec4(0.30f, 0.75f, 1.00f, 1);
        t.accent_active = ImVec4(0.15f, 0.45f, 0.85f, 1); break;
    case ThemeMode::Forest:
        t.window_bg = ImVec4(0.027f, 0.055f, 0.027f, 1); t.topbar_bg = t.window_bg;
        t.accent = ImVec4(0.40f, 0.80f, 0.35f, 1); t.accent2 = ImVec4(0.70f, 1.00f, 0.60f, 1);
        t.accent3 = ImVec4(0.30f, 0.65f, 0.25f, 1); t.accent_hover = ImVec4(0.55f, 0.95f, 0.45f, 1);
        t.accent_active = ImVec4(0.30f, 0.65f, 0.25f, 1); break;
    case ThemeMode::Custom:
    default:
        t.window_bg = ImVec4(0.035f, 0.024f, 0.058f, 1); t.topbar_bg = t.window_bg;
        t.accent = ImVec4(g_custom_color_a[0], g_custom_color_a[1], g_custom_color_a[2], 1.0f);
        t.accent2 = ImVec4(g_custom_color_b[0], g_custom_color_b[1], g_custom_color_b[2], 1.0f);
        t.accent3 = ImVec4(g_custom_color_a[0] * 0.7f, g_custom_color_a[1] * 0.7f, g_custom_color_a[2] * 0.7f, 1.0f);
        t.accent_hover = ImVec4(MY_CLAMP(g_custom_color_a[0] * 1.15f, 0, 1), MY_CLAMP(g_custom_color_a[1] * 1.15f, 0, 1), MY_CLAMP(g_custom_color_a[2] * 1.15f, 0, 1), 1);
        t.accent_active = ImVec4(MY_CLAMP(g_custom_color_a[0] * 0.85f, 0, 1), MY_CLAMP(g_custom_color_a[1] * 0.85f, 0, 1), MY_CLAMP(g_custom_color_a[2] * 0.85f, 0, 1), 1);
        break;
    }
    return t;
}

static ImU32 ColF(ImVec4 c, float a) {
    return IM_COL32((int)(c.x * 255), (int)(c.y * 255), (int)(c.z * 255), (int)(MY_CLAMP(a, 0, 1) * 255));
}

static void OpenGameInSteam(const std::string& id) {
    if (id.empty()) return;
    ShellExecuteA(nullptr, "open", ("steam://store/" + id).c_str(), nullptr, nullptr, SW_SHOWNORMAL);
}

// ============================================================
//  Steam
// ============================================================
static void RunHidden(const std::string& cmd) {
    STARTUPINFOA si = {}; si.cb = sizeof(si);
    si.dwFlags = STARTF_USESHOWWINDOW; si.wShowWindow = SW_HIDE;
    PROCESS_INFORMATION pi = {};
    std::string m = cmd;
    if (CreateProcessA(nullptr, m.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi)) {
        WaitForSingleObject(pi.hProcess, INFINITE);
        CloseHandle(pi.hProcess); CloseHandle(pi.hThread);
    }
}
static std::string WideToUtf8(const std::wstring& w) {
    if (w.empty()) return {};
    int size = WideCharToMultiByte(CP_UTF8, 0, w.c_str(), (int)w.size(), nullptr, 0, nullptr, nullptr);
    std::string out(size, 0);
    WideCharToMultiByte(CP_UTF8, 0, w.c_str(), (int)w.size(), out.data(), size, nullptr, nullptr);
    return out;
}
static std::wstring Utf8ToWide(const std::string& s) {
    if (s.empty()) return {};
    int size = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), nullptr, 0);
    std::wstring out(size, 0);
    MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), out.data(), size);
    return out;
}
static std::string FindSteamPath() {
    HKEY key;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, L"Software\\Valve\\Steam", 0, KEY_READ, &key) == ERROR_SUCCESS) {
        wchar_t buf[MAX_PATH] = {}; DWORD size = sizeof(buf);
        if (RegQueryValueExW(key, L"SteamPath", nullptr, nullptr, (LPBYTE)buf, &size) == ERROR_SUCCESS) {
            RegCloseKey(key);
            std::string p = WideToUtf8(buf);
            for (auto& c : p) if (c == '/') c = '\\';
            if (fs::exists(p + "\\Steam.exe")) return p;
        }
        else RegCloseKey(key);
    }
    if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, L"SOFTWARE\\WOW6432Node\\Valve\\Steam", 0, KEY_READ, &key) == ERROR_SUCCESS) {
        wchar_t buf[MAX_PATH] = {}; DWORD size = sizeof(buf);
        if (RegQueryValueExW(key, L"InstallPath", nullptr, nullptr, (LPBYTE)buf, &size) == ERROR_SUCCESS) {
            RegCloseKey(key);
            std::string p = WideToUtf8(buf);
            if (fs::exists(p + "\\Steam.exe")) return p;
        }
        else RegCloseKey(key);
    }
    const char* std_paths[] = { "C:\\Program Files (x86)\\Steam", "C:\\Program Files\\Steam" };
    for (auto p : std_paths) if (fs::exists(std::string(p) + "\\Steam.exe")) return p;
    return {};
}
static void CloseSteam() { RunHidden("taskkill /f /im steam.exe"); RunHidden("taskkill /f /im steamwebhelper.exe"); Sleep(1200); }
static void RestartSteam() {
    if (g_app_steam_path.empty()) return;
    CloseSteam();
    ShellExecuteA(nullptr, "open", (g_app_steam_path + "\\Steam.exe").c_str(), nullptr, nullptr, SW_SHOWNORMAL);
}
static void OpenLuaFolder() {
    if (g_app_steam_path.empty()) return;
    fs::path lua = fs::path(g_app_steam_path) / "config" / "lua";
    std::error_code ec; fs::create_directories(lua, ec);
    ShellExecuteW(nullptr, L"open", lua.wstring().c_str(), nullptr, nullptr, SW_SHOWNORMAL);
}
static bool HttpGetBinary(const std::string& url, std::vector<unsigned char>& out) {
    std::wstring wurl = Utf8ToWide(url);
    URL_COMPONENTS uc = {}; uc.dwStructSize = sizeof(uc);
    wchar_t host[256] = {}, path[2048] = {};
    uc.lpszHostName = host; uc.dwHostNameLength = 255;
    uc.lpszUrlPath = path; uc.dwUrlPathLength = 2047;
    if (!WinHttpCrackUrl(wurl.c_str(), 0, 0, &uc)) return false;
    bool https = (uc.nScheme == INTERNET_SCHEME_HTTPS);
    HINTERNET hSession = WinHttpOpen(L"SLT/9.5", WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY, WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (!hSession) return false;
    HINTERNET hConnect = WinHttpConnect(hSession, host, uc.nPort, 0);
    if (!hConnect) { WinHttpCloseHandle(hSession); return false; }
    DWORD flags = https ? WINHTTP_FLAG_SECURE : 0;
    HINTERNET hRequest = WinHttpOpenRequest(hConnect, L"GET", path, nullptr, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, flags);
    if (!hRequest) { WinHttpCloseHandle(hConnect); WinHttpCloseHandle(hSession); return false; }
    if (!WinHttpSendRequest(hRequest, WINHTTP_NO_ADDITIONAL_HEADERS, 0, WINHTTP_NO_REQUEST_DATA, 0, 0, 0) || !WinHttpReceiveResponse(hRequest, nullptr)) {
        WinHttpCloseHandle(hRequest); WinHttpCloseHandle(hConnect); WinHttpCloseHandle(hSession); return false;
    }
    DWORD status = 0, sz = sizeof(status);
    WinHttpQueryHeaders(hRequest, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER, WINHTTP_HEADER_NAME_BY_INDEX, &status, &sz, WINHTTP_NO_HEADER_INDEX);
    if (status != 200) { WinHttpCloseHandle(hRequest); WinHttpCloseHandle(hConnect); WinHttpCloseHandle(hSession); return false; }
    out.clear(); DWORD avail = 0;
    do {
        avail = 0; if (!WinHttpQueryDataAvailable(hRequest, &avail)) break;
        if (avail == 0) break;
        size_t old = out.size(); out.resize(old + avail); DWORD read = 0;
        if (!WinHttpReadData(hRequest, out.data() + old, avail, &read)) break;
        out.resize(old + read);
    } while (avail > 0);
    WinHttpCloseHandle(hRequest); WinHttpCloseHandle(hConnect); WinHttpCloseHandle(hSession);
    return !out.empty();
}
static bool DownloadToFile(const std::string& url, const std::string& file) {
    std::vector<unsigned char> buf;
    if (!HttpGetBinary(url, buf)) return false;
    std::ofstream f(file, std::ios::binary);
    if (!f) return false;
    f.write((const char*)buf.data(), buf.size());
    return true;
}

// ============================================================
//  Достижения
//  rarity: 0 = обычное, 1 = редкое, 2 = эпическое, 3 = легендарное
//  secret: true — скрытое до получения
// ============================================================
struct Achievement {
    std::string id, title, desc;
    int rarity = 0;
    bool secret = false;
    bool done = false;
};
static std::vector<Achievement> g_achievements;

static void InitAchievements() {
    g_achievements = {
        // === Основные ===
        {"first_install","Первый шаг","Установите первую игру",0,false},
        {"ten_installs","Коллекционер","Установите 10 игр",0,false},
        {"fifty_installs","Маньяк","Установите 50 игр",1,false},
        {"hundred_installs","Легенда","Установите 100 игр",2,false},
        {"fav_first","Ценитель","Добавьте первую игру в избранное",0,false},
        {"fav_ten","Куратор","10 игр в избранном",0,false},
        {"fav_fifty","Архивариус","50 игр в избранном",1,false},
        {"random_use","Рулетка","Воспользуйтесь случайной игрой",0,false},
        {"random_ten","Азартный","10 раз случайная игра",0,false},
        {"copy_id","Скопируй это","Скопируйте AppID игры",0,false},
        {"copy_hundred","Ксерокс","Скопируйте 100 AppID",1,false},
        {"cancel_once","Передумал","Отмените загрузку",0,false},
        {"cancel_ten","Непостоянный","Отмените 10 загрузок",1,false},
        {"theme_change","Стилист","Смените тему оформления",0,false},
        {"fx_change","Декоратор","Смените фоновую анимацию",0,false},
        {"snow_on","Зимняя сказка","Включите снег",0,false},
        {"rain_on","Дождливый день","Включите дождь",0,false},
        {"leaves_on","Осеннее настроение","Включите листья",0,false},
        {"weather_all","Всесезонье","Включите все погодные эффекты",1,false},
        {"night_owl","Полуночник","Запустите приложение после 0:00",1,false},
        {"early_bird","Ранняя птичка","Запустите приложение до 6:00",1,false},
        {"gradient","Художник","Создайте свой градиент",1,false},
        {"all_tabs","Исследователь","Посетите все вкладки",0,false},

        // === Мини-игры — общие ===
        {"mini_first","Первый заход","Запустите любую мини-игру",0,false},
        {"mini_ten","Игрок","Сыграйте 10 раз в мини-игры",0,false},
        {"mini_fifty","Заядлый","Сыграйте 50 раз в мини-игры",1,false},
        {"mini_hundred","Досуг","Сыграйте 100 раз в мини-игры",2,false},
        {"mini_master","Мастер досуга","Получите 5 звёзд в 5 разных мини-играх",2,false},
        {"mini_legend","Легенда мини-игр","Все мини-игры — 5 звёзд",3,false},

        // === Мини-игры — индивидуальные ===
        {"react_rookie","Рефлекс","Пройдите игру «Реакция»",0,false},
        {"react_pro","Быстрее ветра","Реакция < 250 мс",1,false},
        {"react_god","Молниеносный","Реакция < 150 мс",3,false},
        {"memory_rookie","Мнемоник","Пройдите «Память»",0,false},
        {"memory_pro","Слонишка","Запомните 10 пар",1,false},
        {"memory_god","Фотопамять","Запомните 20 пар",3,false},
        {"clicker_100","Кликер-100","100 кликов за 10 секунд",0,false},
        {"clicker_200","Кликер-бог","200 кликов за 10 секунд",2,false},
        {"math_rookie","Считало","10 правильных примеров подряд",0,false},
        {"math_pro","Математик","30 правильных примеров подряд",1,false},
        {"math_god","Эйнштейн","50 правильных примеров подряд",3,false},
        {"snake_rookie","Змейка-новичок","Съешьте 10 яблок",0,false},
        {"snake_pro","Змейка-про","Съешьте 30 яблок",1,false},
        {"snake_god","Змейка-бог","Съешьте 60 яблок",3,false},
        {"color_rookie","Цветовик","Пройдите «Цвет»",0,false},
        {"color_pro","Оттенок","20 правильных цветов подряд",1,false},
        {"typing_rookie","Печатник","Скорость > 30 WPM",0,false},
        {"typing_pro","Машинистка","Скорость > 60 WPM",1,false},
        {"typing_god","Стенографист","Скорость > 100 WPM",3,false},
        {"aim_rookie","Снайпер","10 попаданий подряд",0,false},
        {"aim_pro","Стрелок","30 попаданий подряд",1,false},
        {"aim_god","Дедшот","60 попаданий подряд",3,false},

        // === Секретные (описание скрыто до получения) ===
        {"secret_hacker","Хакер","...",3,true},
        {"secret_philosopher","Философ","...",3,true},
        {"secret_pacifist","Пацифист","...",3,true},
        {"secret_collector","Коллекционер секретов","...",3,true},
        {"secret_420","Ничего не подозреваю","...",3,true},
    };
}

static void UnlockAchievement(const std::string& id) {
    for (auto& a : g_achievements) {
        if (a.id == id && !a.done) {
            a.done = true;
            std::string txt = a.secret ? ("Секрет: " + a.title) : ("Достижение: " + a.title);
            LogPush("[" + std::string(a.secret ? "Секрет" : "Достижение") + "] " + a.title);
            PushAchievementToast(txt);
            SaveSettings();
        }
    }
}

// ============================================================
//  Счётчики
// ============================================================
static int g_stat_installs = 0, g_stat_fav = 0, g_stat_random = 0, g_stat_copy = 0;
static int g_stat_cancel = 0, g_stat_theme = 0, g_stat_fx = 0, g_stat_weather = 0;
static int g_stat_mini_played = 0;
static float g_fx_time = 0.0f;

// ============================================================
//  Уровень
// ============================================================
static int g_xp = 0;

static int XPForLevel(int lvl) { return 100 + lvl * 50; }

static int LevelFromXP(int xp) {
    int lvl = 1, need = XPForLevel(1), acc = 0;
    while (xp >= acc + need && lvl < 100) { acc += need; lvl++; need = XPForLevel(lvl); }
    return lvl;
}
static int XPIntoLevel(int xp) {
    int lvl = 1, need = XPForLevel(1), acc = 0;
    while (xp >= acc + need && lvl < 100) { acc += need; lvl++; need = XPForLevel(lvl); }
    return xp - acc;
}
static int XPRemaining(int xp) {
    int lvl = LevelFromXP(xp);
    return XPForLevel(lvl);
}
static const char* LevelTitle(int lvl) {
    if (lvl <= 2) return "Новичок";
    if (lvl <= 5) return "Любитель";
    if (lvl <= 10) return "Знаток";
    if (lvl <= 20) return "Коллекционер";
    if (lvl <= 40) return "Мастер";
    if (lvl <= 60) return "Гуру";
    if (lvl <= 80) return "Легенда";
    return "Мифический";
}
static void AddXP(int amount, const std::string& reason = "") {
    if (amount <= 0) return;
    int old_lvl = LevelFromXP(g_xp);
    g_xp += amount;
    int new_lvl = LevelFromXP(g_xp);
    if (new_lvl > old_lvl) {
        PushToast("Новый уровень: " + std::to_string(new_lvl) + " — " + LevelTitle(new_lvl));
        LogPush("Уровень повышен до " + std::to_string(new_lvl));
    }
    if (!reason.empty()) PushToast("+" + std::to_string(amount) + " XP · " + reason);
    SaveSettings();
}

// ============================================================
//  Тосты
// ============================================================
struct MiniToast { std::string text; float timer = 0.0f; bool is_achievement = false; };
static std::deque<MiniToast> g_toasts;

static void PushToast(const std::string& t) {
    MiniToast tt; tt.text = t; tt.timer = 0.0f; tt.is_achievement = false;
    g_toasts.push_back(tt);
    if (g_toasts.size() > 5) g_toasts.pop_front();
}
static void PushAchievementToast(const std::string& t) {
    MiniToast tt; tt.text = t; tt.timer = 0.0f; tt.is_achievement = true;
    g_toasts.push_back(tt);
    if (g_toasts.size() > 5) g_toasts.pop_front();
}

// ============================================================
//  Мини-игры
// ============================================================
enum class MiniGame { None = 0, Reaction, Memory, Clicker, Math, Snake, Color, Typing, Aim, Count };

struct MiniScore {
    int best = 0;
    int stars = 0;
    int plays = 0;
};

struct MiniGameState {
    MiniGame current = MiniGame::None;
    bool running = false;
    bool finished = false;
    float timer = 0.0f;
    float time_limit = 10.0f;
    int score = 0;
    int best_session = 0;
    std::string message;
    // Данные конкретной игры
    float react_start = 0.0f;
    bool react_ready = false;
    float react_last = 0.0f;
    // Memory
    std::vector<int> mem_cards;
    std::vector<bool> mem_flipped;
    std::vector<bool> mem_done;
    int mem_flip_a = -1, mem_flip_b = -1;
    float mem_flash_t = 0.0f;
    int mem_pairs = 0;
    // Clicker
    int clicks = 0;
    // Math
    int math_a = 0, math_b = 0, math_answer = 0;
    int math_streak = 0;
    char math_input[8] = "";
    // Snake
    std::vector<ImVec2> snake;
    ImVec2 snake_dir = ImVec2(1, 0);
    std::vector<ImVec2> apples;
    float snake_move_t = 0.0f;
    int snake_grid = 20;
    // Color
    float col_target_r = 0.5f, col_target_g = 0.5f, col_target_b = 0.5f;
    float col_guess_r = 0.5f, col_guess_g = 0.5f, col_guess_b = 0.5f;
    int col_streak = 0;
    // Typing
    std::string type_text;
    std::string type_target;
    int type_correct = 0, type_wrong = 0;
    float type_start = 0.0f;
    // Aim
    std::vector<ImVec2> aim_targets;
    float aim_spawn_t = 0.0f;
    int aim_hits = 0, aim_misses = 0;
};

static MiniGameState g_mini;
static MiniScore g_mini_scores[(int)MiniGame::Count];

static const char* MiniName(MiniGame g) {
    switch (g) {
    case MiniGame::Reaction: return "Реакция";
    case MiniGame::Memory:   return "Память";
    case MiniGame::Clicker:  return "Кликер";
    case MiniGame::Math:     return "Математика";
    case MiniGame::Snake:    return "Змейка";
    case MiniGame::Color:    return "Цвет";
    case MiniGame::Typing:   return "Печать";
    case MiniGame::Aim:      return "Снайпер";
    default: return "—";
    }
}
static const char* MiniDesc(MiniGame g) {
    switch (g) {
    case MiniGame::Reaction: return "Нажми как можно быстрее после сигнала";
    case MiniGame::Memory:   return "Найди все пары карточек";
    case MiniGame::Clicker:  return "Как можно больше кликов за 10 секунд";
    case MiniGame::Math:     return "Решай примеры на скорость";
    case MiniGame::Snake:    return "Собери как можно больше яблок";
    case MiniGame::Color:    return "Угадай цвет по оттенку";
    case MiniGame::Typing:   return "Печатай текст на скорость";
    case MiniGame::Aim:      return "Кликни по всем мишеням";
    default: return "";
    }
}

static void ComputeStars(MiniGame game, int score, int& stars) {
    stars = 0;
    switch (game) {
    case MiniGame::Reaction:  // score в миллисекундах, меньше = лучше
        if (score > 0 && score < 500) stars = 1;
        if (score > 0 && score < 300) stars = 2;
        if (score > 0 && score < 250) stars = 3;
        if (score > 0 && score < 200) stars = 4;
        if (score > 0 && score < 150) stars = 5;
        break;
    case MiniGame::Memory:
        if (score >= 4) stars = 1;
        if (score >= 8) stars = 2;
        if (score >= 10) stars = 3;
        if (score >= 15) stars = 4;
        if (score >= 20) stars = 5;
        break;
    case MiniGame::Clicker:
        if (score >= 50) stars = 1;
        if (score >= 100) stars = 2;
        if (score >= 150) stars = 3;
        if (score >= 200) stars = 4;
        if (score >= 250) stars = 5;
        break;
    case MiniGame::Math:
        if (score >= 10) stars = 1;
        if (score >= 20) stars = 2;
        if (score >= 30) stars = 3;
        if (score >= 40) stars = 4;
        if (score >= 50) stars = 5;
        break;
    case MiniGame::Snake:
        if (score >= 10) stars = 1;
        if (score >= 20) stars = 2;
        if (score >= 30) stars = 3;
        if (score >= 45) stars = 4;
        if (score >= 60) stars = 5;
        break;
    case MiniGame::Color:
        if (score >= 5) stars = 1;
        if (score >= 10) stars = 2;
        if (score >= 15) stars = 3;
        if (score >= 20) stars = 4;
        if (score >= 25) stars = 5;
        break;
    case MiniGame::Typing:
        if (score >= 20) stars = 1;
        if (score >= 30) stars = 2;
        if (score >= 45) stars = 3;
        if (score >= 60) stars = 4;
        if (score >= 80) stars = 5;
        break;
    case MiniGame::Aim:
        if (score >= 10) stars = 1;
        if (score >= 20) stars = 2;
        if (score >= 30) stars = 3;
        if (score >= 45) stars = 4;
        if (score >= 60) stars = 5;
        break;
    default: break;
    }
}

static void MiniFinish() {
    if (!g_mini.running) return;
    g_mini.running = false;
    g_mini.finished = true;

    int score = g_mini.score;
    MiniGame g = g_mini.current;
    int idx = (int)g;

    g_mini_scores[idx].plays++;
    g_stat_mini_played++;

    int stars = 0;
    ComputeStars(g, score, stars);
    if (stars > g_mini_scores[idx].stars) g_mini_scores[idx].stars = stars;
    if (g == MiniGame::Reaction) {
        // меньше = лучше
        if (g_mini_scores[idx].best == 0 || (score > 0 && score < g_mini_scores[idx].best))
            g_mini_scores[idx].best = score;
    }
    else {
        if (score > g_mini_scores[idx].best) g_mini_scores[idx].best = score;
    }

    g_mini.message = std::string("Результат: ") + std::to_string(score) +
        (g == MiniGame::Reaction ? " мс" : " очков") +
        "  ·  Звёзд: " + std::to_string(stars);

    // XP и достижения
    AddXP(5 + stars * 5, "мини-игра");

    if (g_stat_mini_played >= 1) UnlockAchievement("mini_first");
    if (g_stat_mini_played >= 10) UnlockAchievement("mini_ten");
    if (g_stat_mini_played >= 50) UnlockAchievement("mini_fifty");
    if (g_stat_mini_played >= 100) UnlockAchievement("mini_hundred");

    int five_star_count = 0;
    for (int i = 1; i < (int)MiniGame::Count; i++)
        if (g_mini_scores[i].stars >= 5) five_star_count++;
    if (five_star_count >= 5) UnlockAchievement("mini_master");
    if (five_star_count >= (int)MiniGame::Count - 1) UnlockAchievement("mini_legend");

    // Индивидуальные
    switch (g) {
    case MiniGame::Reaction:
        UnlockAchievement("react_rookie");
        if (score > 0 && score < 250) UnlockAchievement("react_pro");
        if (score > 0 && score < 150) UnlockAchievement("react_god");
        break;
    case MiniGame::Memory:
        UnlockAchievement("memory_rookie");
        if (score >= 10) UnlockAchievement("memory_pro");
        if (score >= 20) UnlockAchievement("memory_god");
        break;
    case MiniGame::Clicker:
        if (score >= 100) UnlockAchievement("clicker_100");
        if (score >= 200) UnlockAchievement("clicker_200");
        break;
    case MiniGame::Math:
        if (score >= 10) UnlockAchievement("math_rookie");
        if (score >= 30) UnlockAchievement("math_pro");
        if (score >= 50) UnlockAchievement("math_god");
        break;
    case MiniGame::Snake:
        if (score >= 10) UnlockAchievement("snake_rookie");
        if (score >= 30) UnlockAchievement("snake_pro");
        if (score >= 60) UnlockAchievement("snake_god");
        break;
    case MiniGame::Color:
        UnlockAchievement("color_rookie");
        if (score >= 20) UnlockAchievement("color_pro");
        break;
    case MiniGame::Typing:
        if (score >= 30) UnlockAchievement("typing_rookie");
        if (score >= 60) UnlockAchievement("typing_pro");
        if (score >= 100) UnlockAchievement("typing_god");
        break;
    case MiniGame::Aim:
        if (score >= 10) UnlockAchievement("aim_rookie");
        if (score >= 30) UnlockAchievement("aim_pro");
        if (score >= 60) UnlockAchievement("aim_god");
        break;
    default: break;
    }

    SaveSettings();
    PushToast(std::string(MiniName(g)) + ": +" + std::to_string(5 + stars * 5) + " XP");
}

static void MiniStart(MiniGame g) {
    g_mini = MiniGameState{};
    g_mini.current = g;
    g_mini.running = true;
    g_mini.finished = false;
    g_mini.timer = 0.0f;
    g_mini.score = 0;

    std::mt19937 rng((unsigned)std::chrono::system_clock::now().time_since_epoch().count());

    switch (g) {
    case MiniGame::Reaction:
        g_mini.time_limit = 5.0f;
        g_mini.react_ready = false;
        g_mini.react_start = 0.0f;
        break;
    case MiniGame::Memory: {
        g_mini.time_limit = 999.0f;
        int pairs = 10;
        g_mini.mem_pairs = pairs;
        g_mini.mem_cards.resize(pairs * 2);
        g_mini.mem_flipped.assign(pairs * 2, false);
        g_mini.mem_done.assign(pairs * 2, false);
        for (int i = 0; i < pairs; i++) { g_mini.mem_cards[i * 2] = i; g_mini.mem_cards[i * 2 + 1] = i; }
        std::shuffle(g_mini.mem_cards.begin(), g_mini.mem_cards.end(), rng);
        break;
    }
    case MiniGame::Clicker:
        g_mini.time_limit = 10.0f;
        g_mini.clicks = 0;
        break;
    case MiniGame::Math: {
        g_mini.time_limit = 30.0f;
        std::uniform_int_distribution<int> d(1, 20);
        g_mini.math_a = d(rng);
        g_mini.math_b = d(rng);
        g_mini.math_answer = g_mini.math_a + g_mini.math_b;
        g_mini.math_streak = 0;
        break;
    }
    case MiniGame::Snake:
        g_mini.time_limit = 999.0f;
        g_mini.snake_grid = 20;
        g_mini.snake.clear();
        g_mini.snake.push_back(ImVec2(10, 10));
        g_mini.snake_dir = ImVec2(1, 0);
        g_mini.apples.clear();
        g_mini.apples.push_back(ImVec2((float)(rng() % 20), (float)(rng() % 20)));
        g_mini.snake_move_t = 0.0f;
        break;
    case MiniGame::Color: {
        g_mini.time_limit = 20.0f;
        std::uniform_real_distribution<float> d(0.1f, 0.9f);
        g_mini.col_target_r = d(rng); g_mini.col_target_g = d(rng); g_mini.col_target_b = d(rng);
        g_mini.col_guess_r = 0.5f; g_mini.col_guess_g = 0.5f; g_mini.col_guess_b = 0.5f;
        g_mini.col_streak = 0;
        break;
    }
    case MiniGame::Typing: {
        g_mini.time_limit = 30.0f;
        const char* phrases[] = {
            "The quick brown fox jumps over the lazy dog",
            "Pack my box with five dozen liquor jugs",
            "How vexingly quick daft zebras jump",
            "The five boxing wizards jump quickly",
            "Sphinx of black quartz judge my vow",
        };
        std::uniform_int_distribution<int> d(0, 4);
        g_mini.type_target = phrases[d(rng)];
        g_mini.type_text.clear();
        g_mini.type_correct = 0;
        g_mini.type_wrong = 0;
        g_mini.type_start = 0.0f;
        break;
    }
    case MiniGame::Aim:
        g_mini.time_limit = 20.0f;
        g_mini.aim_targets.clear();
        g_mini.aim_spawn_t = 0.0f;
        g_mini.aim_hits = 0;
        g_mini.aim_misses = 0;
        break;
    default: break;
    }
}


// ============================================================
//  AppState
// ============================================================
struct GameEntry {
    std::string appid, name, header_image;
    std::vector<std::string> tags;
    bool nsfw = false;
    bool favorite = false;
    ID3D11ShaderResourceView* texture = nullptr;
    bool loading = false, load_failed = false;
    float fade_in = 0.0f;
    float open_time = -1.0f;
    bool installed = false;
    float star_hover_t = 0.0f;
    float copy_hover_t = 0.0f;
};

struct PendingTexture { std::string appid; int w = 0, h = 0; std::vector<unsigned char> pixels; };
struct LogEntry { std::string text; bool is_error = false; std::chrono::system_clock::time_point time; };

enum class ProgressMode { None = 0, Download, Delete, InstallTools };

struct AppState {
    std::vector<GameEntry> all_games;
    std::vector<int> filtered;

    char search_buf[128] = "";
    std::string search_query;

    std::vector<std::string> available_tags;
    int selected_tag = 0;

    int page = 0;
    static const int kGamesPerPage = 20;
    static const int kGridCols = 5;
    static const int kGridRows = 4;

    int fav_page = 0;

    bool random_mode = false;
    int random_index = -1;

    char goto_buf[8] = "";
    float goto_error_timer = 0.0f;

    int prev_page = 0;
    float page_switch_t = 1.0f;

    bool steam_found = false;

    struct InstalledEntry { std::string appid, name; };
    std::vector<InstalledEntry> installed;

    std::deque<LogEntry> log;
    std::mutex log_mtx;

    std::atomic<int> progress_mode{ (int)ProgressMode::None };
    std::atomic<bool> cancel_requested{ false };
    std::atomic<float> progress_target{ 0.0f };
    float progress_display = 0.0f;
    std::string current_name, current_appid;
    std::mutex status_mtx;

    bool success_visible = false;
    float success_alpha = 0.0f, success_timer = 0.0f, overlay_alpha = 0.0f;

    int current_tab = 0, prev_tab = 0;
    float tab_fade = 1.0f, underline_x = 0.0f;
    int underline_initialized = 0;
    float time_accum = 0.0f, app_open_anim = 0.0f;
    int notifications_count = 0;

    ImVec2 outline_pos_cur = ImVec2(0, 0), outline_size_cur = ImVec2(0, 0);
    ImVec2 outline_pos_target = ImVec2(0, 0), outline_size_target = ImVec2(0, 0);
    float outline_alpha = 0.0f;
    bool outline_any_hover = false;

    std::deque<PendingTexture> pending_textures;
    std::mutex pending_mutex;
    std::atomic<int> active_downloads{ 0 };

    bool IsBusy() const { return progress_mode.load() != (int)ProgressMode::None; }
};
static AppState g_app;

static bool g_tag_combo_open = false;
static bool g_tag_combo_just_picked = false;

struct SplashState { bool active = true; float alpha = 1.0f, timer = 0.0f; float min_show_time = 0.35f; bool ready = false; std::string step_text = "Запуск..."; };
static SplashState g_splash;

static void LogPush(const std::string& text, bool is_error) {
    std::lock_guard<std::mutex> lk(g_app.log_mtx);
    LogEntry e; e.text = text; e.is_error = is_error; e.time = std::chrono::system_clock::now();
    g_app.log.push_front(e);
    if (g_app.log.size() > 200) g_app.log.pop_back();
    g_app.notifications_count = (int)g_app.log.size();
}

// ============================================================
//  Save / Load
// ============================================================
static void SaveSettings() {
    try {
        json j;
        j["theme"] = (int)g_theme;
        j["fx"] = (int)g_fx;
        j["fx_snow"] = g_fx_snow;
        j["fx_rain"] = g_fx_rain;
        j["fx_leaves"] = g_fx_leaves;
        j["xp"] = g_xp;
        j["custom_a"] = { g_custom_color_a[0], g_custom_color_a[1], g_custom_color_a[2], g_custom_color_a[3] };
        j["custom_b"] = { g_custom_color_b[0], g_custom_color_b[1], g_custom_color_b[2], g_custom_color_b[3] };
        j["stat_installs"] = g_stat_installs;
        j["stat_fav"] = g_stat_fav;
        j["stat_random"] = g_stat_random;
        j["stat_copy"] = g_stat_copy;
        j["stat_cancel"] = g_stat_cancel;
        j["stat_theme"] = g_stat_theme;
        j["stat_fx"] = g_stat_fx;
        j["stat_weather"] = g_stat_weather;
        j["stat_mini_played"] = g_stat_mini_played;

        // Мини-игры
        json jm = json::array();
        for (int i = 1; i < (int)MiniGame::Count; i++) {
            json e;
            e["best"] = g_mini_scores[i].best;
            e["stars"] = g_mini_scores[i].stars;
            e["plays"] = g_mini_scores[i].plays;
            jm.push_back(e);
        }
        j["mini_scores"] = jm;

        j["favorites"] = json::array();
        for (auto& g : g_app.all_games) if (g.favorite) j["favorites"].push_back(g.appid);
        j["achievements"] = json::array();
        for (auto& a : g_achievements) if (a.done) j["achievements"].push_back(a.id);

        std::ofstream f(GetSettingsPath());
        if (f.is_open()) f << j.dump(2);
    }
    catch (...) {}
}

static void LoadSettings() {
    try {
        std::ifstream f(GetSettingsPath());
        if (!f.is_open()) return;
        json j; f >> j;
        if (j.contains("theme")) { int v = j["theme"].get<int>(); if (v >= 0 && v <= (int)ThemeMode::Custom) g_theme = (ThemeMode)v; }
        if (j.contains("fx")) { int v = j["fx"].get<int>(); if (v >= 0 && v < (int)FxMode::Count) g_fx = (FxMode)v; }
        if (j.contains("fx_snow")) g_fx_snow = j["fx_snow"].get<bool>();
        if (j.contains("fx_rain")) g_fx_rain = j["fx_rain"].get<bool>();
        if (j.contains("fx_leaves")) g_fx_leaves = j["fx_leaves"].get<bool>();
        if (j.contains("xp")) g_xp = j["xp"].get<int>();
        if (j.contains("stat_installs")) g_stat_installs = j["stat_installs"].get<int>();
        if (j.contains("stat_fav")) g_stat_fav = j["stat_fav"].get<int>();
        if (j.contains("stat_random")) g_stat_random = j["stat_random"].get<int>();
        if (j.contains("stat_copy")) g_stat_copy = j["stat_copy"].get<int>();
        if (j.contains("stat_cancel")) g_stat_cancel = j["stat_cancel"].get<int>();
        if (j.contains("stat_theme")) g_stat_theme = j["stat_theme"].get<int>();
        if (j.contains("stat_fx")) g_stat_fx = j["stat_fx"].get<int>();
        if (j.contains("stat_weather")) g_stat_weather = j["stat_weather"].get<int>();
        if (j.contains("stat_mini_played")) g_stat_mini_played = j["stat_mini_played"].get<int>();
        if (j.contains("mini_scores") && j["mini_scores"].is_array()) {
            auto& arr = j["mini_scores"];
            for (int i = 1; i < (int)MiniGame::Count && (i - 1) < (int)arr.size(); i++) {
                auto& e = arr[i - 1];
                g_mini_scores[i].best = e.value("best", 0);
                g_mini_scores[i].stars = e.value("stars", 0);
                g_mini_scores[i].plays = e.value("plays", 0);
            }
        }
        if (j.contains("custom_a") && j["custom_a"].is_array() && j["custom_a"].size() == 4)
            for (int i = 0; i < 4; ++i) g_custom_color_a[i] = j["custom_a"][i].get<float>();
        if (j.contains("custom_b") && j["custom_b"].is_array() && j["custom_b"].size() == 4)
            for (int i = 0; i < 4; ++i) g_custom_color_b[i] = j["custom_b"][i].get<float>();
        if (j.contains("favorites") && j["favorites"].is_array()) {
            std::unordered_set<std::string> favs;
            for (auto& v : j["favorites"]) favs.insert(v.get<std::string>());
            for (auto& g : g_app.all_games) if (favs.count(g.appid)) g.favorite = true;
        }
        if (j.contains("achievements") && j["achievements"].is_array()) {
            std::unordered_set<std::string> achs;
            for (auto& v : j["achievements"]) achs.insert(v.get<std::string>());
            for (auto& a : g_achievements) if (achs.count(a.id)) a.done = true;
        }
    }
    catch (...) {}
}

// ============================================================
//  Стиль
// ============================================================
static void ApplyStyle() {
    ThemeColors t = GetTheme();
    ImGuiStyle& s = ImGui::GetStyle();
    s.WindowRounding = 0.0f; s.ChildRounding = 10.0f; s.FrameRounding = 8.0f;
    s.PopupRounding = 10.0f; s.ScrollbarRounding = 10.0f; s.GrabRounding = 8.0f;
    s.WindowBorderSize = 0.0f; s.FrameBorderSize = 0.0f; s.PopupBorderSize = 1.0f;
    s.ItemSpacing = ImVec2(12, 10); s.FramePadding = ImVec2(12, 8);
    ImVec4* c = s.Colors;
    c[ImGuiCol_WindowBg] = t.window_bg;
    c[ImGuiCol_ChildBg] = ImVec4(0, 0, 0, 0);
    c[ImGuiCol_PopupBg] = ImVec4(t.window_bg.x + 0.02f, t.window_bg.y + 0.02f, t.window_bg.z + 0.03f, 0.98f);
    c[ImGuiCol_Border] = ImVec4(t.accent.x, t.accent.y, t.accent.z, 0.35f);
    c[ImGuiCol_FrameBg] = ImVec4(t.accent.x, t.accent.y, t.accent.z, 0.08f);
    c[ImGuiCol_FrameBgHovered] = ImVec4(t.accent.x, t.accent.y, t.accent.z, 0.14f);
    c[ImGuiCol_FrameBgActive] = ImVec4(t.accent.x, t.accent.y, t.accent.z, 0.20f);
    c[ImGuiCol_Text] = t.text;
    c[ImGuiCol_TextDisabled] = t.text_disabled;
    c[ImGuiCol_TextSelectedBg] = ImVec4(t.accent.x, t.accent.y, t.accent.z, 0.35f);
    c[ImGuiCol_NavHighlight] = ImVec4(t.accent.x, t.accent.y, t.accent.z, 0.80f);
    c[ImGuiCol_Button] = ImVec4(1, 1, 1, 0.05f);
    c[ImGuiCol_ButtonHovered] = ImVec4(t.accent.x, t.accent.y, t.accent.z, 0.30f);
    c[ImGuiCol_ButtonActive] = ImVec4(t.accent.x, t.accent.y, t.accent.z, 0.50f);
    c[ImGuiCol_Header] = ImVec4(t.accent.x, t.accent.y, t.accent.z, 0.30f);
    c[ImGuiCol_HeaderHovered] = ImVec4(t.accent.x, t.accent.y, t.accent.z, 0.45f);
    c[ImGuiCol_HeaderActive] = ImVec4(t.accent.x, t.accent.y, t.accent.z, 0.60f);
    c[ImGuiCol_CheckMark] = ImVec4(t.accent.x, t.accent.y, t.accent.z, 1.0f);
    c[ImGuiCol_ScrollbarBg] = ImVec4(0, 0, 0, 0);
    c[ImGuiCol_ScrollbarGrab] = ImVec4(t.accent.x, t.accent.y, t.accent.z, 0.55f);
    c[ImGuiCol_ScrollbarGrabHovered] = ImVec4(t.accent.x, t.accent.y, t.accent.z, 0.75f);
    c[ImGuiCol_ScrollbarGrabActive] = ImVec4(t.accent.x, t.accent.y, t.accent.z, 1.0f);
    c[ImGuiCol_ModalWindowDimBg] = ImVec4(0, 0, 0, 0.0f);
}

// ============================================================
//  Словарь тегов EN → RU
// ============================================================
static const std::unordered_map<std::string, std::string>& TagDictionary() {
    static const std::unordered_map<std::string, std::string> dict = {
        {"Action","Экшен"},{"Adventure","Приключение"},{"Casual","Казуальная"},
        {"Indie","Инди"},{"RPG","РПГ"},{"Strategy","Стратегия"},
        {"Simulation","Симулятор"},{"Sports","Спорт"},{"Racing","Гонки"},
        {"Massively Multiplayer","ММО"},{"Free To Play","Бесплатная"},
        {"Early Access","Ранний доступ"},{"Animation & Modeling","Анимация и моделирование"},
        {"Audio Production","Аудиопроизводство"},{"Design & Illustration","Дизайн и иллюстрация"},
        {"Education","Образование"},{"Game Development","Разработка игр"},
        {"Photo Editing","Редактирование фото"},{"Utilities","Утилиты"},
        {"Video Production","Видеопроизводство"},{"Web Publishing","Веб-публикация"},
        {"Software Training","Обучение ПО"},{"Accounting","Бухгалтерия"},
        {"Nudity","Нагота"},{"Sexual Content","Сексуальный контент"},
        {"Violent","Насилие"},{"Gore","Кровь"},{"Horror","Хоррор"},
        {"Mature","Для взрослых"},
        {"Singleplayer","Одиночная"},{"Multiplayer","Мультиплеер"},
        {"Co-op","Кооператив"},{"Online Co-Op","Онлайн-кооп"},
        {"Local Co-Op","Локальный кооп"},{"Local Multiplayer","Локальный мультиплеер"},
        {"Cross-Platform Multiplayer","Кроссплатформенный"},{"MMO","ММО"},
        {"PvP","PvP"},{"Online PvP","Онлайн PvP"},{"LAN PvP","LAN PvP"},
        {"Shared/Split Screen","Общий/разделённый экран"},{"Steam Achievements","Достижения Steam"},
        {"Steam Trading Cards","Карточки Steam"},{"Steam Cloud","Облако Steam"},
        {"Steam Workshop","Мастерская Steam"},{"Full controller support","Полная поддержка геймпада"},
        {"Partial Controller Support","Частичная поддержка геймпада"},{"VR Support","Поддержка VR"},
        {"Remote Play","Удалённая игра"},{"Remote Play Together","Удалённая игра вместе"},
        {"In-App Purchases","Встроенные покупки"},{"Includes level editor","Редактор уровней"},
        {"Commentary available","Комментарии разработчиков"},{"Captions available","Субтитры"},
        {"Stats","Статистика"},{"Atmospheric","Атмосферная"},{"Great Soundtrack","Отличный саундтрек"},
        {"Story Rich","Богатый сюжет"},{"Open World","Открытый мир"},{"Sandbox","Песочница"},
        {"Survival","Выживание"},{"Crafting","Крафтинг"},{"Building","Строительство"},
        {"Exploration","Исследование"},{"Platformer","Платформер"},{"Puzzle","Головоломка"},
        {"Shooter","Шутер"},{"First-Person","От первого лица"},{"Third Person","От третьего лица"},
        {"Top-Down","Вид сверху"},{"Isometric","Изометрия"},{"Side Scroller","Скроллер"},
        {"Point & Click","Point & Click"},{"Visual Novel","Визуальная новелла"},
        {"Walking Simulator","Симулятор ходьбы"},{"Stealth","Стелс"},{"Souls-like","Souls-like"},
        {"Metroidvania","Метроидвания"},{"Roguelike","Рогалик"},{"Roguelite","Рогалик-лайт"},
        {"Card Game","Карточная"},{"Board Game","Настольная"},{"Turn-Based","Пошаговая"},
        {"Real-Time","В реальном времени"},{"Turn-Based Strategy","Пошаговая стратегия"},
        {"Turn-Based Tactics","Пошаговая тактика"},{"Real Time Strategy","Стратегия в реальном времени"},
        {"Real-Time with Pause","Реальное время с паузой"},{"Grand Strategy","Глобальная стратегия"},
        {"4X","4X"},{"City Builder","Градостроительный симулятор"},{"Colony Sim","Симулятор колонии"},
        {"Management","Управление"},{"Economy","Экономика"},{"Political Sim","Политический симулятор"},
        {"Dating Sim","Симулятор свиданий"},{"Farming Sim","Фермерский симулятор"},
        {"Space Sim","Космический симулятор"},{"Flight","Полёты"},{"Naval","Морской"},
        {"Tank","Танковый"},{"Military","Военная"},{"War","Война"},{"Historical","Историческая"},
        {"Medieval","Средневековье"},{"Fantasy","Фэнтези"},{"Sci-fi","Научная фантастика"},
        {"Cyberpunk","Киберпанк"},{"Post-apocalyptic","Постапокалипсис"},{"Zombies","Зомби"},
        {"Vampire","Вампиры"},{"Detective","Детектив"},{"Mystery","Мистика"},
        {"Thriller","Триллер"},{"Comedy","Комедия"},{"Drama","Драма"},{"Romance","Романтика"},
        {"Anime","Аниме"},{"Cute","Милая"},{"Relaxing","Расслабляющая"},{"Funny","Смешная"},
        {"Dark","Мрачная"},{"Mature Themes","Взрослые темы"},{"Female Protagonist","Женский протагонист"},
        {"Male Protagonist","Мужской протагонист"},{"Character Customization","Кастомизация персонажа"},
        {"Choices Matter","Выбор имеет значение"},{"Multiple Endings","Несколько концовок"},
        {"Linear","Линейная"},{"Nonlinear","Нелинейная"},{"Procedural Generation","Процедурная генерация"},
        {"Pixel Graphics","Пиксельная графика"},{"Retro","Ретро"},{"Hand-drawn","Рисованная"},
        {"Stylized","Стилизованная"},{"Realistic","Реалистичная"},{"Anime Style","Аниме-стиль"},
        {"Cartoon","Мультяшная"},{"Colorful","Красочная"},{"Minimalist","Минимализм"},
        {"2D","2D"},{"3D","3D"},{"2.5D","2.5D"},{"VR","VR"},{"Music","Музыка"},
        {"Rhythm","Ритм"},{"Party","Вечеринка"},{"Family Friendly","Для всей семьи"},
        {"Trivia","Викторина"},{"Chess","Шахматы"},{"Football","Футбол"},{"Soccer","Футбол"},
        {"Basketball","Баскетбол"},{"Baseball","Бейсбол"},{"Tennis","Теннис"},
        {"Golf","Гольф"},{"Fishing","Рыбалка"},{"Hunting","Охота"},{"Boxing","Бокс"},
        {"Wrestling","Реслинг"},{"Skateboarding","Скейтборд"},{"Snowboarding","Сноуборд"},
        {"Cycling","Велоспорт"},{"Motorsport","Мотоспорт"},{"Flight Sim","Авиасимулятор"},
        {"Tower Defense","Tower Defense"},{"MOBA","MOBA"},{"Battle Royale","Battle Royale"},
        {"Hero Shooter","Геройский шутер"},{"Looter Shooter","Лутер-шутер"},
        {"Bullet Hell","Bullet Hell"},{"Hack and Slash","Hack and Slash"},
        {"Beat 'em up","Beat 'em up"},{"Fighting","Файтинг"},{"Arcade","Аркада"},
        {"Action RPG","Экшен-РПГ"},{"JRPG","JRPG"},{"CRPG","CRPG"},
        {"Tactical RPG","Тактическая РПГ"},{"Dungeon Crawler","Dungeon Crawler"},
        {"Idle","Idle"},{"Clicker","Кликер"},{"Typing","Печать"},{"Word Game","Словесная игра"},
        {"Math","Математика"},{"Programming","Программирование"},{"Moddable","Модифицируемая"},
        {"Mod","Мод"},{"Level Editor","Редактор уровней"},{"Creative","Творческая"},
        {"Automation","Автоматизация"},{"Logistics","Логистика"},{"Trains","Поезда"},
        {"Cars","Машины"},{"Space","Космос"},{"Aliens","Пришельцы"},{"Robots","Роботы"},
        {"Mechs","Мехи"},{"Ninja","Ниндзя"},{"Pirates","Пираты"},{"Western","Вестерн"},
        {"Steampunk","Стимпанк"},{"Lovecraftian","Лавкрафт"},{"Gothic","Готика"},
        {"Noir","Нуар"},{"Superhero","Супергерои"},{"Dystopian","Антиутопия"},
        {"Alternate History","Альтернативная история"},{"Time Travel","Путешествие во времени"},
        {"Magic","Магия"},{"Dragons","Драконы"},{"Demons","Демоны"},{"Mythology","Мифология"},
        {"World War I","Первая мировая"},{"World War II","Вторая мировая"},
        {"Cold War","Холодная война"},{"Modern","Современность"},{"Futuristic","Футуризм"},
        {"Survival Horror","Хоррор на выживание"},{"Psychological Horror","Психологический хоррор"},
        {"Cthulhu","Ктулху"},{"Werewolves","Оборотни"},{"Ghosts","Призраки"},
        {"Occult","Оккультизм"},{"Supernatural","Сверхъестественное"},
    };
    return dict;
}

// ============================================================
//  Фильтр мусора
// ============================================================
static bool IsJunkGame(const std::string& name, const std::string& appid) {
    static const std::vector<std::string> junk_ids = {
        "228980","1070560","1391110","1623560","2180100","961370","1007","7",
    };
    for (auto& id : junk_ids) if (appid == id) return true;
    std::string n = name;
    for (auto& c : n) c = (char)tolower((unsigned char)c);
    static const std::vector<std::string> bad_exact = {
        "source sdk base","source sdk","dedicated server","steamvr",
        "steam linux runtime","steamworks common","proton experimental","steam client",
    };
    for (auto& b : bad_exact) if (n.find(b) != std::string::npos) return true;
    if (n.size() < 2) return true;
    return false;
}

// ============================================================
//  Загрузка JSON
// ============================================================
static bool LoadGamesJson() {
    const char* candidates[] = { "gamespic.json", "./gamespic.json", "../gamespic.json" };
    std::ifstream f;
    for (auto p : candidates) if (fs::exists(p)) { f.open(p, std::ios::binary); break; }
    std::string json_text;
    if (f.is_open()) { std::stringstream ss; ss << f.rdbuf(); json_text = ss.str(); }
    else {
        HRSRC hRes = FindResourceW(nullptr, MAKEINTRESOURCEW(IDR_GAMESPIC), RT_RCDATA);
        if (!hRes) return false;
        HGLOBAL hData = LoadResource(nullptr, hRes);
        DWORD size = SizeofResource(nullptr, hRes);
        const char* ptr = hData ? (const char*)LockResource(hData) : nullptr;
        if (!ptr) return false;
        json_text.assign(ptr, size);
    }
    json j;
    try { j = json::parse(json_text); }
    catch (...) { return false; }

    std::vector<GameEntry> loaded;
    std::set<std::string> tag_set_ru;
    const auto& dict = TagDictionary();

    auto parseOne = [&](const json& it) {
        if (!it.contains("appid") || !it.contains("name")) return;
        std::string appid = it.value("appid", "");
        std::string type = it.value("type", "game");
        std::string name = it.value("name", "");
        std::string type_l = type;
        for (auto& c : type_l) c = (char)tolower((unsigned char)c);
        if (!type_l.empty() && type_l != "game") return;
        if (IsJunkGame(name, appid)) return;

        GameEntry g;
        g.appid = appid; g.name = name;
        g.header_image = it.value("header_image", "");
        g.nsfw = it.value("nsfw", false);

        if (it.contains("tags") && it["tags"].is_array()) {
            for (auto& tg : it["tags"]) {
                if (!tg.is_string()) continue;
                std::string s = tg.get<std::string>();
                if (s.empty()) continue;
                auto found = dict.find(s);
                if (found == dict.end()) continue;
                const std::string& ru = found->second;
                bool dup = false;
                for (auto& e : g.tags) if (e == ru) { dup = true; break; }
                if (dup) continue;
                g.tags.push_back(ru);
                tag_set_ru.insert(ru);
            }
        }
        loaded.push_back(std::move(g));
        };

    if (j.is_array()) for (auto& item : j) parseOne(item);
    else if (j.is_object()) for (auto& kv : j.items()) if (kv.value().is_object()) parseOne(kv.value());

    if (loaded.empty()) return false;

    g_app.available_tags.clear();
    g_app.available_tags.push_back("Все теги");
    for (auto& s : tag_set_ru) g_app.available_tags.push_back(s);
    g_app.available_tags.push_back("NSFW");
    g_app.selected_tag = 0;
    g_app.all_games = std::move(loaded);
    return true;
}

// ============================================================
//  Картинки
// ============================================================
static void DownloadAndDecodeWorker(std::string url, std::string appid) {
    std::vector<unsigned char> raw;
    if (HttpGetBinary(url, raw)) {
        int w, h, comp;
        unsigned char* pixels = stbi_load_from_memory(raw.data(), (int)raw.size(), &w, &h, &comp, 4);
        if (pixels) {
            PendingTexture pt;
            pt.appid = appid; pt.w = w; pt.h = h;
            pt.pixels.assign(pixels, pixels + (size_t)w * h * 4);
            stbi_image_free(pixels);
            std::lock_guard<std::mutex> lock(g_app.pending_mutex);
            g_app.pending_textures.push_back(std::move(pt));
        }
    }
    g_app.active_downloads--;
}
static void RequestImageLoad(GameEntry& g) {
    if (g.texture || g.loading || g.load_failed) return;
    if (g.header_image.empty()) return;
    if (g_app.active_downloads.load() >= 6) return;
    g.loading = true; g_app.active_downloads++;
    std::thread(DownloadAndDecodeWorker, g.header_image, g.appid).detach();
}
static void ProcessPendingTextures() {
    int done = 0;
    while (done < 3) {
        PendingTexture pt;
        {
            std::lock_guard<std::mutex> lock(g_app.pending_mutex); if (g_app.pending_textures.empty()) break;
            pt = std::move(g_app.pending_textures.front()); g_app.pending_textures.pop_front();
        }
        D3D11_TEXTURE2D_DESC desc = {};
        desc.Width = pt.w; desc.Height = pt.h; desc.MipLevels = 1; desc.ArraySize = 1;
        desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM; desc.SampleDesc.Count = 1;
        desc.Usage = D3D11_USAGE_DEFAULT; desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
        D3D11_SUBRESOURCE_DATA sub = {}; sub.pSysMem = pt.pixels.data(); sub.SysMemPitch = pt.w * 4;
        ID3D11Texture2D* tex = nullptr;
        if (SUCCEEDED(g_pd3dDevice->CreateTexture2D(&desc, &sub, &tex))) {
            ID3D11ShaderResourceView* srv = nullptr;
            if (SUCCEEDED(g_pd3dDevice->CreateShaderResourceView(tex, nullptr, &srv))) {
                for (auto& g : g_app.all_games) if (g.appid == pt.appid) {
                    g.texture = srv; g.open_time = (float)ImGui::GetTime(); g.loading = false; break;
                }
            }
            tex->Release();
        }
        done++;
    }
}

// ============================================================
//  Фильтр
// ============================================================
static void RebuildFiltered() {
    g_app.filtered.clear();
    std::string q = g_app.search_query;
    std::transform(q.begin(), q.end(), q.begin(), ::tolower);
    int tag_idx = g_app.selected_tag;
    bool tag_is_all = (tag_idx <= 0 || tag_idx >= (int)g_app.available_tags.size());
    bool tag_is_nsfw = (!tag_is_all && g_app.available_tags[tag_idx] == "NSFW");
    std::string tag_value = tag_is_all ? "" : g_app.available_tags[tag_idx];

    for (int i = 0; i < (int)g_app.all_games.size(); i++) {
        GameEntry& g = g_app.all_games[i];
        if (!tag_is_all) {
            if (tag_is_nsfw) { if (!g.nsfw) continue; }
            else { bool has = false; for (auto& tg : g.tags) if (tg == tag_value) { has = true; break; } if (!has) continue; }
        }
        if (q.empty()) { g_app.filtered.push_back(i); continue; }
        std::string name = g.name;
        std::transform(name.begin(), name.end(), name.begin(), ::tolower);
        if (name.find(q) != std::string::npos || g.appid.find(q) != std::string::npos) g_app.filtered.push_back(i);
    }
    g_app.page = 0; g_app.prev_page = 0; g_app.page_switch_t = 1.0f;
}

// ============================================================
//  Установленные
// ============================================================
static void RefreshInstalledFlags() {
    if (g_app_steam_path.empty()) return;
    fs::path lua_dir = fs::path(g_app_steam_path) / "config" / "lua";
    std::map<std::string, bool> inst;
    if (fs::exists(lua_dir)) for (auto& e : fs::directory_iterator(lua_dir))
        if (e.path().extension() == ".lua") inst[e.path().stem().string()] = true;
    for (auto& g : g_app.all_games) g.installed = inst.count(g.appid) > 0;
}
static void RefreshInstalledFiles() {
    g_app.installed.clear();
    if (g_app_steam_path.empty()) return;
    fs::path lua = fs::path(g_app_steam_path) / "config" / "lua";
    if (!fs::exists(lua)) return;
    for (auto& p : fs::directory_iterator(lua)) {
        if (p.path().extension() == ".lua") {
            AppState::InstalledEntry e;
            e.appid = p.path().stem().string();
            e.name = e.appid;
            for (auto& g : g_app.all_games) if (g.appid == e.appid) { e.name = g.name; break; }
            g_app.installed.push_back(e);
        }
    }
}
static void DeleteInstalled(const std::string& appid) {
    if (g_app_steam_path.empty()) return;
    fs::path lua = fs::path(g_app_steam_path) / "config" / "lua";
    fs::path depot = fs::path(g_app_steam_path) / "depotcache";
    std::error_code ec;
    if (fs::exists(lua)) for (auto& p : fs::directory_iterator(lua)) if (p.path().stem().string() == appid) fs::remove(p.path(), ec);
    if (fs::exists(depot)) for (auto& p : fs::directory_iterator(depot)) if (p.path().stem().string() == appid) fs::remove(p.path(), ec);
    RefreshInstalledFiles(); RefreshInstalledFlags();
    LogPush("Удалена игра AppID: " + appid);
    AddXP(2, "удаление");
}
static int CopyLuaFromDir(const std::string& extract_dir) {
    fs::path lua_dst = fs::path(g_app_steam_path) / "config" / "lua";
    fs::path depot_dst = fs::path(g_app_steam_path) / "depotcache";
    fs::create_directories(lua_dst); fs::create_directories(depot_dst);
    int copied = 0;
    for (auto& p : fs::recursive_directory_iterator(extract_dir)) {
        if (!p.is_regular_file()) continue;
        auto ext = p.path().extension().string();
        if (ext == ".lua" || ext == ".manifest") {
            std::error_code ec;
            fs::copy_file(p.path(), lua_dst / p.path().filename(), fs::copy_options::overwrite_existing, ec);
            fs::copy_file(p.path(), depot_dst / p.path().filename(), fs::copy_options::overwrite_existing, ec);
            copied++;
        }
    }
    return copied;
}
static void ShowSuccess() { g_app.success_visible = true; g_app.success_alpha = 0.0f; g_app.success_timer = 0.0f; }

// ============================================================
//  Манифесты
// ============================================================
static void DownloadManifestAsync(std::string appid, std::string name) {
    if (g_app.IsBusy()) { LogPush("Уже идёт операция", true); return; }
    g_app.progress_mode.store((int)ProgressMode::Download);
    g_app.cancel_requested = false;
    g_app.progress_target = 0.01f;
    g_app.progress_display = 0.0f;
    { std::lock_guard<std::mutex> lk(g_app.status_mtx); g_app.current_name = name + " (AppID " + appid + ")"; g_app.current_appid = appid; }
    LogPush("Начата загрузка: " + name + " [AppID " + appid + "]");

    std::thread([appid, name]() {
        std::string url = "https://codeload.github.com/SPIN0ZAi/SB_manifest_DB/zip/refs/heads/" + appid;
        char temp_path[MAX_PATH]; GetTempPathA(MAX_PATH, temp_path);
        std::string zip_path = std::string(temp_path) + "SLT_" + appid + ".zip";
        std::string extract_dir = std::string(temp_path) + "SLT_" + appid;

        auto cleanup_partial = [&]() { std::error_code ec; fs::remove(zip_path, ec); fs::remove_all(extract_dir, ec); };

        g_app.progress_target = 0.15f;
        bool ok = DownloadToFile(url, zip_path);
        if (g_app.cancel_requested) {
            cleanup_partial(); LogPush("Загрузка отменена: " + name, true);
            g_stat_cancel++; SaveSettings();
            g_app.progress_mode.store((int)ProgressMode::None); g_app.progress_target = 0.0f; g_app.progress_display = 0.0f;
            return;
        }
        g_app.progress_target = 0.65f;
        if (!ok) {
            LogPush("Не удалось скачать манифест для " + name, true); cleanup_partial();
            g_app.progress_mode.store((int)ProgressMode::None); g_app.progress_target = 0.0f; g_app.progress_display = 0.0f; return;
        }
        LogPush("Скачано: " + name + ", распаковка...");

        std::error_code ec; fs::remove_all(extract_dir, ec);
        std::string cmd = "powershell -NoProfile -Command \"Expand-Archive -Force -Path '" + zip_path + "' -DestinationPath '" + extract_dir + "'\"";
        RunHidden(cmd);

        if (g_app.cancel_requested) {
            cleanup_partial(); LogPush("Загрузка отменена: " + name, true);
            g_stat_cancel++; SaveSettings();
            g_app.progress_mode.store((int)ProgressMode::None); g_app.progress_target = 0.0f; g_app.progress_display = 0.0f; return;
        }
        g_app.progress_target = 0.88f;
        int copied = CopyLuaFromDir(extract_dir);
        LogPush("Установлено файлов: " + std::to_string(copied) + " для " + name);
        cleanup_partial();

        g_app.progress_target = 1.0f;
        std::this_thread::sleep_for(std::chrono::milliseconds(300));
        RefreshInstalledFlags(); RefreshInstalledFiles();

        g_stat_installs++; SaveSettings();
        AddXP(10, "установка");
        if (g_stat_installs >= 1) UnlockAchievement("first_install");
        if (g_stat_installs >= 10) UnlockAchievement("ten_installs");
        if (g_stat_installs >= 50) UnlockAchievement("fifty_installs");
        if (g_stat_installs >= 100) UnlockAchievement("hundred_installs");

        g_app.progress_mode.store((int)ProgressMode::None); g_app.progress_target = 0.0f; g_app.progress_display = 0.0f;
        ShowSuccess();
        PushToast("Установлено: " + name);
        }).detach();
}

static void CancelCurrentDownload() {
    if (g_app.progress_mode.load() != (int)ProgressMode::Download) return;
    g_app.cancel_requested = true;
    std::string appid;
    { std::lock_guard<std::mutex> lk(g_app.status_mtx); appid = g_app.current_appid; }
    if (appid.empty()) { g_app.progress_mode.store((int)ProgressMode::None); return; }
    g_app.progress_mode.store((int)ProgressMode::Delete);
    g_app.progress_target = 0.01f; g_app.progress_display = 0.0f;

    std::thread([appid]() {
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
        g_app.progress_target = 0.35f;
        fs::path lua = fs::path(g_app_steam_path) / "config" / "lua";
        fs::path depot = fs::path(g_app_steam_path) / "depotcache";
        std::error_code ec;
        std::vector<fs::path> to_delete;
        if (fs::exists(lua)) for (auto& p : fs::directory_iterator(lua)) if (p.path().stem().string() == appid) to_delete.push_back(p.path());
        if (fs::exists(depot)) for (auto& p : fs::directory_iterator(depot)) if (p.path().stem().string() == appid) to_delete.push_back(p.path());
        int total = (int)to_delete.size();
        if (total == 0) g_app.progress_target = 1.0f;
        else for (int i = 0; i < total; ++i) {
            fs::remove(to_delete[i], ec);
            g_app.progress_target = 0.35f + 0.65f * ((float)(i + 1) / total);
            std::this_thread::sleep_for(std::chrono::milliseconds(150));
        }
        LogPush("Отмена: удалены файлы AppID " + appid, true);
        g_stat_cancel++; SaveSettings();
        RefreshInstalledFiles(); RefreshInstalledFlags();
        g_app.progress_target = 1.0f; std::this_thread::sleep_for(std::chrono::milliseconds(400));
        g_app.progress_mode.store((int)ProgressMode::None); g_app.progress_target = 0.0f; g_app.progress_display = 0.0f;
        ShowSuccess();
        }).detach();
}

static void InstallZipFile(const std::string& zip_path) {
    if (g_app_steam_path.empty()) { LogPush("Steam не найден", true); return; }
    char temp_path[MAX_PATH]; GetTempPathA(MAX_PATH, temp_path);
    std::string extract_dir = std::string(temp_path) + "SLT_manual";
    std::error_code ec; fs::remove_all(extract_dir, ec);
    std::string cmd = "powershell -NoProfile -Command \"Expand-Archive -Force -Path '" + zip_path + "' -DestinationPath '" + extract_dir + "'\"";
    RunHidden(cmd);
    int copied = CopyLuaFromDir(extract_dir);
    LogPush("Установлено файлов из ZIP: " + std::to_string(copied));
    fs::remove_all(extract_dir, ec);
    RefreshInstalledFiles(); RefreshInstalledFlags();
    PushToast("ZIP установлен (" + std::to_string(copied) + " файлов)");
    AddXP(5, "ZIP");
}

static void InstallOpenSteamToolsAsync() {
    if (g_app.IsBusy()) { LogPush("Уже идёт операция", true); return; }
    g_app.progress_mode.store((int)ProgressMode::InstallTools);
    g_app.progress_target = 0.05f; g_app.progress_display = 0.0f;
    { std::lock_guard<std::mutex> lk(g_app.status_mtx); g_app.current_name = "OpenSteamTools"; g_app.current_appid = ""; }
    LogPush("Установка OpenSteamTools...");

    std::thread([]() {
        const char* paths[] = { "OpenSteamTools.zip", "..\\OpenSteamTools.zip" };
        std::string zip;
        for (auto p : paths) if (fs::exists(p)) { zip = p; break; }
        g_app.progress_target = 0.10f;
        char temp_path[MAX_PATH]; GetTempPathA(MAX_PATH, temp_path);
        std::string extract_dir = std::string(temp_path) + "OST";
        std::string zip_path = std::string(temp_path) + "OST.zip";
        if (zip.empty()) {
            HRSRC hRes = FindResourceW(nullptr, MAKEINTRESOURCEW(IDR_OST), RT_RCDATA);
            if (!hRes) { LogPush("OpenSteamTools.zip не найден", true); g_app.progress_mode.store((int)ProgressMode::None); g_app.progress_target = 0.0f; return; }
            HGLOBAL hData = LoadResource(nullptr, hRes);
            DWORD size = SizeofResource(nullptr, hRes);
            const void* ptr = hData ? LockResource(hData) : nullptr;
            if (!ptr) { LogPush("Не удалось загрузить ресурс", true); g_app.progress_mode.store((int)ProgressMode::None); g_app.progress_target = 0.0f; return; }
            std::ofstream of(zip_path, std::ios::binary); of.write((const char*)ptr, size); of.close();
            zip = zip_path;
        }
        g_app.progress_target = 0.20f; CloseSteam();
        g_app.progress_target = 0.45f;
        std::error_code ec; fs::remove_all(extract_dir, ec);
        std::string cmd = "powershell -NoProfile -Command \"Expand-Archive -Force -Path '" + fs::absolute(zip).string() + "' -DestinationPath '" + extract_dir + "'\"";
        RunHidden(cmd);
        g_app.progress_target = 0.70f;
        int copied = 0;
        for (auto& p : fs::recursive_directory_iterator(extract_dir)) {
            if (!p.is_regular_file()) continue;
            try { fs::copy_file(p.path(), fs::path(g_app_steam_path) / p.path().filename(), fs::copy_options::overwrite_existing); copied++; }
            catch (...) {}
        }
        fs::remove_all(extract_dir, ec);
        if (zip == zip_path) fs::remove(zip_path, ec);
        g_app.progress_target = 0.95f;
        LogPush("OpenSteamTools: скопировано " + std::to_string(copied) + " файлов");
        std::this_thread::sleep_for(std::chrono::milliseconds(300));
        g_app.progress_target = 1.0f; std::this_thread::sleep_for(std::chrono::milliseconds(400));
        g_app.progress_mode.store((int)ProgressMode::None); g_app.progress_target = 0.0f; g_app.progress_display = 0.0f;
        ShowSuccess();
        PushToast("OpenSteamTools установлен");
        AddXP(15, "OpenSteamTools");
        }).detach();
}

// ============================================================
//  FX
// ============================================================


static void DrawAuroraBg(ImDrawList* dl, ImVec2 o, ImVec2 s, float time) {
    ThemeColors t = GetTheme();
    dl->AddRectFilled(o, ImVec2(o.x + s.x, o.y + s.y), ColF(t.window_bg, 1.0f));
    struct Wave { float y0, amp, freq, speed, thickness; ImVec4 col; };
    Wave waves[3] = {
        { s.y * 0.28f, 60.0f, 1.6f, 0.15f, 140.0f, t.accent },
        { s.y * 0.55f, 80.0f, 1.1f, -0.11f, 170.0f, t.accent2 },
        { s.y * 0.78f, 50.0f, 2.0f, 0.09f, 120.0f, t.accent3 },
    };
    for (auto& w : waves) {
        for (int trail = 6; trail >= 0; trail--) {
            float tt = time - trail * 0.035f;
            float alpha = (1.0f - (float)trail / 7) * 0.05f;
            ImVec2 pts[64];
            for (int i = 0; i < 64; i++) {
                float fx = (float)i / 63.0f;
                float x = o.x + fx * s.x;
                float y = o.y + w.y0 + std::sin(fx * w.freq * 6.2831853f + tt * w.speed * 6.2831853f) * w.amp
                    + std::sin(fx * w.freq * 2.0f + tt * w.speed * 3.0f) * w.amp * 0.3f;
                pts[i] = ImVec2(x, y);
            }
            for (int i = 0; i < 63; i++) dl->AddLine(pts[i], pts[i + 1], ColF(w.col, alpha), w.thickness * (0.4f + 0.6f * ((float)i / 64)));
        }
    }
}
static void DrawWavesBg(ImDrawList* dl, ImVec2 o, ImVec2 s, float time) {
    ThemeColors t = GetTheme();
    dl->AddRectFilled(o, ImVec2(o.x + s.x, o.y + s.y), ColF(t.window_bg, 1.0f));
    for (int layer = 0; layer < 6; ++layer) {
        float yBase = o.y + s.y * (0.30f + layer * 0.10f);
        float amp = 22.0f + layer * 6.0f, freq = 1.4f + layer * 0.2f, speed = 0.4f + layer * 0.08f;
        ImVec4 col = (layer % 2 == 0) ? t.accent : t.accent2;
        ImVec2 pts[96];
        for (int i = 0; i < 96; i++) {
            float fx = (float)i / 95.0f;
            pts[i] = ImVec2(o.x + fx * s.x, yBase + std::sin(fx * freq * 6.2831853f + time * speed) * amp
                + std::sin(fx * freq * 3.1f + time * speed * 1.7f) * amp * 0.35f);
        }
        for (int i = 0; i < 95; i++) dl->AddLine(pts[i], pts[i + 1], ColF(col, 0.10f), 2.0f);
    }
}
static void DrawParticlesBg(ImDrawList* dl, ImVec2 o, ImVec2 s, float time) {
    ThemeColors t = GetTheme();
    dl->AddRectFilled(o, ImVec2(o.x + s.x, o.y + s.y), ColF(t.window_bg, 1.0f));
    std::mt19937 rng(1337);
    std::uniform_real_distribution<float> dx(0, 1), dv(0, 1);
    for (int i = 0; i < 90; ++i) {
        float bx = dx(rng), by = dx(rng), sp = 0.2f + dv(rng) * 0.5f, sz = 1.5f + dv(rng) * 3.0f;
        float px = fmodf(bx * s.x + time * sp * 30.0f, s.x); if (px < 0) px += s.x;
        float py = fmodf(by * s.y + time * sp * 20.0f, s.y); if (py < 0) py += s.y;
        ImU32 c1 = ColF(t.accent, 0.5f);
        ImU32 c2 = ColF(t.accent2, 0.25f);
        dl->AddCircleFilled(ImVec2(o.x + px, o.y + py), sz, c1, 12);
        dl->AddCircleFilled(ImVec2(o.x + px, o.y + py), sz * 2.5f, c2, 16);
    }
}
static void DrawGradientBg(ImDrawList* dl, ImVec2 o, ImVec2 s, float time) {
    ThemeColors t = GetTheme();
    float pulse = 0.55f + 0.45f * std::sin(time * 0.6f);
    ImVec4 a = ImVec4(t.accent.x * (0.35f + 0.25f * pulse), t.accent.y * (0.35f + 0.25f * pulse), t.accent.z * (0.35f + 0.25f * pulse), 1.0f);
    ImVec4 b = ImVec4(t.accent2.x * 0.25f, t.accent2.y * 0.25f, t.accent2.z * 0.25f, 1.0f);
    dl->AddRectFilledMultiColor(o, ImVec2(o.x + s.x, o.y + s.y), ColF(a, 1.0f), ColF(b, 1.0f), ColF(t.window_bg, 1.0f), ColF(t.window_bg, 1.0f));
}
static void DrawGridBg(ImDrawList* dl, ImVec2 o, ImVec2 s, float time) {
    ThemeColors t = GetTheme();
    dl->AddRectFilled(o, ImVec2(o.x + s.x, o.y + s.y), ColF(t.window_bg, 1.0f));
    const float cell = 60.0f;
    float offX = fmodf(time * 12.0f, cell), offY = fmodf(time * 8.0f, cell);
    ImU32 lineCol = ColF(t.accent, 0.10f);
    for (float x = -offX; x < s.x + cell; x += cell) dl->AddLine(ImVec2(o.x + x, o.y), ImVec2(o.x + x, o.y + s.y), lineCol, 1.0f);
    for (float y = -offY; y < s.y + cell; y += cell) dl->AddLine(ImVec2(o.x, o.y + y), ImVec2(o.x + s.x, o.y + y), lineCol, 1.0f);
}

struct WParticle { float x, y, vx, vy, size, phase, rot, rotSpeed; int type; };
static std::vector<WParticle> g_weather;
static bool g_weather_init = false;
static void InitWeather() {
    if (g_weather_init) return;
    std::mt19937 rng(42);
    std::uniform_real_distribution<float> pos(0, 1);
    std::uniform_real_distribution<float> sz(1.0f, 4.0f);
    std::uniform_real_distribution<float> sp(20.0f, 90.0f);
    std::uniform_real_distribution<float> ph(0, 6.28f);
    std::uniform_real_distribution<float> rs(-3.0f, 3.0f);
    g_weather.clear();
    for (int i = 0; i < 260; ++i) {
        WParticle p;
        p.x = pos(rng); p.y = pos(rng);
        if (i % 3 == 1) p.vx = 0.0f;
        else p.vx = (pos(rng) - 0.5f) * 40.0f;
        p.vy = sp(rng);
        p.size = sz(rng);
        p.phase = ph(rng);
        p.rot = ph(rng);
        p.rotSpeed = rs(rng);
        p.type = i % 3;
        g_weather.push_back(p);
    }
    g_weather_init = true;
}
static void DrawWeatherFx(ImDrawList* dl, ImVec2 o, ImVec2 s, float dt) {
    InitWeather();
    for (auto& p : g_weather) {
        p.y += p.vy * dt * 0.01f;
        p.x += p.vx * dt * 0.01f;
        p.rot += p.rotSpeed * dt;
        if (p.y > 1.2f) { p.y = -0.1f; p.x = (float)rand() / RAND_MAX; }
        if (p.x > 1.1f) p.x = -0.05f;
        if (p.x < -0.1f) p.x = 1.05f;
    }
    if (g_fx_snow) {
        for (auto& p : g_weather) {
            if (p.type != 0) continue;
            float px = o.x + p.x * s.x;
            float py = o.y + p.y * s.y;
            float sway = std::sin(g_fx_time * 1.5f + p.phase) * 6.0f;
            float alpha = 0.55f + 0.35f * std::sin(g_fx_time * 2.0f + p.phase);
            dl->AddCircleFilled(ImVec2(px + sway, py), p.size, IM_COL32(255, 255, 255, (int)(alpha * 220)), 10);
        }
    }
    if (g_fx_rain) {
        for (auto& p : g_weather) {
            if (p.type != 1) continue;
            float px = o.x + p.x * s.x;
            float py = o.y + p.y * s.y;
            float len = 10.0f + p.size * 5.0f;
            dl->AddLine(ImVec2(px, py), ImVec2(px, py + len), IM_COL32(180, 200, 255, 140), 1.2f);
        }
    }
    if (g_fx_leaves) {
        for (auto& p : g_weather) {
            if (p.type != 2) continue;
            float px = o.x + p.x * s.x;
            float py = o.y + p.y * s.y;
            float sway = std::sin(g_fx_time * 0.8f + p.phase) * 30.0f;
            px += sway;
            float sz = 4.0f + p.size * 1.5f;
            ImU32 col = IM_COL32(220, 140, 60, 220);
            ImVec2 c(px, py);
            ImVec2 a1(c.x + cosf(p.rot) * sz, c.y + sinf(p.rot) * sz);
            ImVec2 a2(c.x + cosf(p.rot + 2.1f) * sz, c.y + sinf(p.rot + 2.1f) * sz);
            ImVec2 a3(c.x + cosf(p.rot + 4.2f) * sz, c.y + sinf(p.rot + 4.2f) * sz);
            dl->AddTriangleFilled(a1, a2, a3, col);
        }
    }
}
static void DrawBackgroundFx(ImDrawList* dl, ImVec2 o, ImVec2 s, float time) {
    g_fx_time = time;
    switch (g_fx) {
    case FxMode::Aurora: DrawAuroraBg(dl, o, s, time); break;
    case FxMode::Waves: DrawWavesBg(dl, o, s, time); break;
    case FxMode::Particles: DrawParticlesBg(dl, o, s, time); break;
    case FxMode::Gradient: DrawGradientBg(dl, o, s, time); break;
    case FxMode::Grid: DrawGridBg(dl, o, s, time); break;
    default: break;
    }
    ImGuiIO& io = ImGui::GetIO();
    DrawWeatherFx(dl, o, s, io.DeltaTime);
}

// ============================================================
//  Кнопки окна
// ============================================================
static void DrawWindowButton(ImDrawList* dl, ImVec2 p0, ImVec2 p1, bool is_close, bool hovered, bool active) {
    ImVec4 base = is_close ? ImVec4(0.85f, 0.22f, 0.32f, 1) : ImVec4(1, 1, 1, 1);
    float bg_alpha = active ? (is_close ? 1.0f : 0.18f) : (hovered ? (is_close ? 0.9f : 0.12f) : 0.0f);
    dl->AddRectFilled(p0, p1, ColF(base, bg_alpha), 6.0f);
    ImVec2 c((p0.x + p1.x) * 0.5f, (p0.y + p1.y) * 0.5f);
    ImU32 icon_col = ColF(ImVec4(1, 1, 1, 1), hovered || active ? 1.0f : 0.85f);
    if (is_close) {
        float r = 5.0f;
        dl->AddLine(ImVec2(c.x - r, c.y - r), ImVec2(c.x + r, c.y + r), icon_col, 1.6f);
        dl->AddLine(ImVec2(c.x - r, c.y + r), ImVec2(c.x + r, c.y - r), icon_col, 1.6f);
    }
    else {
        float r = 5.5f;
        dl->AddLine(ImVec2(c.x - r, c.y), ImVec2(c.x + r, c.y), icon_col, 1.6f);
    }
}

// ============================================================
//  Топбар (6 вкладок)
// ============================================================
static void DrawTopBar() {
    ThemeColors t = GetTheme();
    ImGui::PushStyleColor(ImGuiCol_ChildBg, t.topbar_bg);
    const float topbar_h = 60.0f;
    ImGui::BeginChild("##topbar", ImVec2(0, topbar_h), false, ImGuiWindowFlags_NoScrollbar);

    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImVec2 wp = ImGui::GetWindowPos(), ws = ImGui::GetWindowSize();
    float time = (float)ImGui::GetTime();

    dl->AddRectFilledMultiColor(ImVec2(wp.x, wp.y + ws.y - 1), ImVec2(wp.x + ws.x, wp.y + ws.y),
        ColF(t.accent, 0.5f), ColF(t.accent3, 0.5f), ColF(t.accent2, 0.5f), ColF(t.accent, 0.5f));

    static bool s_drag = false;
    static POINT s_m{}, s_w{};
    ImVec2 mp = ImGui::GetIO().MousePos;
    bool in_drag = mp.x >= wp.x && mp.x <= wp.x + ws.x - 260 && mp.y >= wp.y && mp.y <= wp.y + 50;
    if (!s_drag && in_drag && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
        s_drag = true; GetCursorPos(&s_m); RECT rc; GetWindowRect(g_hwnd, &rc); s_w = { rc.left, rc.top };
    }
    if (s_drag && ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
        POINT p; GetCursorPos(&p);
        SetWindowPos(g_hwnd, nullptr, s_w.x + (p.x - s_m.x), s_w.y + (p.y - s_m.y), 0, 0, SWP_NOSIZE | SWP_NOZORDER);
    }
    if (ImGui::IsMouseReleased(ImGuiMouseButton_Left)) s_drag = false;

    ImGui::SetCursorPos(ImVec2(28, 18.0f));
    if (g_font_title) ImGui::PushFont(g_font_title);
    ImGui::TextColored(t.text, "SteamLuaTools");
    ImVec2 title_size = ImGui::CalcTextSize("SteamLuaTools");
    if (g_font_title) ImGui::PopFont();

    {
        ImVec2 vpos(wp.x + 28 + title_size.x + 20, wp.y + 18 + title_size.y * 0.5f);
        float vr = 13.0f;
        float pulse = 0.85f + 0.15f * std::sin(time * 2.5f);
        dl->AddCircleFilled(vpos, vr * 2.0f, ColF(t.accent, 0.10f * pulse), 48);
        dl->AddCircleFilled(vpos, vr * 1.5f, ColF(t.accent, 0.18f * pulse), 48);
        dl->AddCircleFilled(vpos, vr, ColF(t.accent, 0.95f), 48);
        dl->AddCircle(vpos, vr, ColF(ImVec4(1, 1, 1, 1), 0.6f), 48, 2.0f);
        const char* ver = "9.5";
        ImVec2 vsz = ImGui::CalcTextSize(ver);
        dl->AddText(ImVec2(vpos.x - vsz.x * 0.5f, vpos.y - vsz.y * 0.5f - 1.0f), IM_COL32(255, 255, 255, 255), ver);
    }

    // Уровень справа от версии
    {
        int lvl = LevelFromXP(g_xp);
        char lvl_buf[64];
        snprintf(lvl_buf, sizeof(lvl_buf), "Ур. %d · %s", lvl, LevelTitle(lvl));
        ImVec2 lvl_sz = ImGui::CalcTextSize(lvl_buf);
        ImVec2 lvl_pos(wp.x + 28 + title_size.x + 50, wp.y + 18 + title_size.y * 0.5f - lvl_sz.y * 0.5f);

        // Плашка
        dl->AddRectFilled(ImVec2(lvl_pos.x - 8, lvl_pos.y - 4),
            ImVec2(lvl_pos.x + lvl_sz.x + 8, lvl_pos.y + lvl_sz.y + 4),
            ColF(t.accent, 0.20f), 6.0f);
        dl->AddRect(ImVec2(lvl_pos.x - 8, lvl_pos.y - 4),
            ImVec2(lvl_pos.x + lvl_sz.x + 8, lvl_pos.y + lvl_sz.y + 4),
            ColF(t.accent, 0.55f), 6.0f, 0, 1.2f);
        dl->AddText(lvl_pos, ColF(t.text, 1.0f), lvl_buf);
    }

    // 6 вкладок
    const float tab_w = 118.0f, tab_h = 32.0f, tab_gap = 5.0f;
    const float tabs_y = (topbar_h - tab_h) * 0.5f;
    const float tabs_x = 400.0f;
    struct TabInfo { const char* label; int idx; };
    TabInfo tabs[6] = {
        {"Магазин",0},{"Избранное",1},{"Достижения",2},
        {"Мини-игры",3},{"Библиотека",4},{"Инструменты",5}
    };
    static float s_tab_x[6] = { 0 };
    float dt = ImGui::GetIO().DeltaTime; if (dt <= 0.0f) dt = 0.016f;
    bool busy = g_app.IsBusy();
    if (busy) ImGui::BeginDisabled();

    for (int i = 0; i < 6; i++) {
        float x = tabs_x + i * (tab_w + tab_gap);
        s_tab_x[i] = x;
        ImGui::SetCursorPos(ImVec2(x, tabs_y));
        ImGui::PushID(i);
        bool active = (g_app.current_tab == tabs[i].idx);
        ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 6.0f);
        ImGui::PushStyleColor(ImGuiCol_Button, active ? ImVec4(t.accent.x, t.accent.y, t.accent.z, 0.20f) : ImVec4(1, 1, 1, 0.0f));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(t.accent.x, t.accent.y, t.accent.z, 0.28f));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(t.accent.x, t.accent.y, t.accent.z, 0.38f));
        ImGui::PushStyleColor(ImGuiCol_Text, active ? t.text : t.text_disabled);
        if (ImGui::Button(tabs[i].label, ImVec2(tab_w, tab_h))) {
            if (g_app.current_tab != tabs[i].idx) { g_app.prev_tab = g_app.current_tab; g_app.current_tab = tabs[i].idx; g_app.tab_fade = 0.0f; }
        }
        ImGui::PopStyleColor(4); ImGui::PopStyleVar();
        ImGui::PopID();
    }
    if (busy) ImGui::EndDisabled();

    float target_underline_x = s_tab_x[g_app.current_tab];
    if (!g_app.underline_initialized) { g_app.underline_x = target_underline_x; g_app.underline_initialized = 1; }
    g_app.underline_x = ExpLerp(g_app.underline_x, target_underline_x, 12.0f, dt);
    float ux = wp.x + g_app.underline_x + 10, uy = wp.y + tabs_y + tab_h + 2;
    dl->AddRectFilledMultiColor(ImVec2(ux, uy), ImVec2(ux + tab_w - 20, uy + 3),
        ColF(t.accent, 1.0f), ColF(t.accent3, 1.0f), ColF(t.accent3, 1.0f), ColF(t.accent, 1.0f));

    const float btn_w = 32.0f, btn_h = 30.0f, gap = 6.0f;
    float bx1 = wp.x + ws.x - 32.0f - btn_w;
    float bx0 = bx1 - gap - btn_w;
    float by = wp.y + (topbar_h - btn_h) * 0.5f;
    ImVec2 min_p0(bx0, by), min_p1(bx0 + btn_w, by + btn_h);
    ImVec2 cls_p0(bx1, by), cls_p1(bx1 + btn_w, by + btn_h);
    bool min_h = ImGui::IsMouseHoveringRect(min_p0, min_p1);
    bool cls_h = ImGui::IsMouseHoveringRect(cls_p0, cls_p1);
    DrawWindowButton(dl, min_p0, min_p1, false, min_h, min_h && ImGui::IsMouseDown(ImGuiMouseButton_Left));
    DrawWindowButton(dl, cls_p0, cls_p1, true, cls_h, cls_h && ImGui::IsMouseDown(ImGuiMouseButton_Left));
    if (min_h && ImGui::IsMouseReleased(ImGuiMouseButton_Left)) ShowWindow(g_hwnd, SW_MINIMIZE);
    if (cls_h && ImGui::IsMouseReleased(ImGuiMouseButton_Left)) PostMessageW(g_hwnd, WM_CLOSE, 0, 0);

    ImGui::EndChild();
    ImGui::PopStyleColor();
}

// ============================================================
//  Forward
// ============================================================
static void RebuildFiltered();
static bool IsFavorite(const std::string& appid);

// ============================================================
//  ComboBox тегов
// ============================================================
static void DrawTagComboBox(float width, float height) {
    ThemeColors t = GetTheme();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImDrawList* fg = ImGui::GetForegroundDrawList();
    ImGuiIO& io = ImGui::GetIO();
    ImGui::PushID("tagcombo");

    static bool s_open = false;
    static float s_anim = 0.0f, s_scroll = 0.0f;

    const char* current = "Все теги";
    if (g_app.selected_tag > 0 && g_app.selected_tag < (int)g_app.available_tags.size())
        current = g_app.available_tags[g_app.selected_tag].c_str();

    ImVec2 p0 = ImGui::GetCursorScreenPos();
    ImVec2 p1 = ImVec2(p0.x + width, p0.y + height);
    bool hovered = ImGui::IsMouseHoveringRect(p0, p1);

    ImGui::SetCursorScreenPos(p0);
    ImGui::InvisibleButton("##combo_btn", ImVec2(width, height));
    if (ImGui::IsItemClicked()) s_open = !s_open;

    g_tag_combo_open = s_open;
    g_tag_combo_just_picked = false;

    const float row_h = 30.0f, pad_y = 6.0f, max_h = 320.0f;
    int n = (int)g_app.available_tags.size();
    float full_h = n * row_h + pad_y * 2.0f;
    bool scroll = full_h > max_h;
    float list_h = scroll ? max_h : full_h;
    ImVec2 lp0(p0.x, p1.y + 4.0f), lp1(lp0.x + width, lp0.y + list_h);

    if (s_open) {
        bool in_btn = ImGui::IsMouseHoveringRect(p0, p1);
        bool in_popup = ImGui::IsMouseHoveringRect(lp0, lp1);
        if (!in_btn && !in_popup && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) s_open = false;
        if (ImGui::IsKeyPressed(ImGuiKey_Escape)) s_open = false;
    }

    float target = s_open ? 1.0f : 0.0f;
    float dt = io.DeltaTime; if (dt <= 0.0f) dt = 0.016f;
    s_anim += (target - s_anim) * (1.0f - std::exp(-18.0f * dt));
    if (s_anim < 0.001f) s_anim = 0.0f;
    if (s_anim > 0.999f) s_anim = 1.0f;

    ImU32 bg_col = ColF(t.accent, hovered ? 0.14f : 0.06f);
    ImU32 bd_col = ColF(t.accent, hovered || s_open ? 0.9f : 0.45f);
    dl->AddRectFilled(p0, p1, bg_col, 8.0f);
    dl->AddRect(p0, p1, bd_col, 8.0f, 0, 1.5f);
    {
        ImVec2 ic(p0.x + 16.0f, p0.y + height * 0.5f);
        ImU32 ic_col = ColF(t.accent, 0.95f);
        dl->AddLine(ImVec2(ic.x - 7, ic.y - 5), ImVec2(ic.x + 7, ic.y - 5), ic_col, 1.8f);
        dl->AddLine(ImVec2(ic.x - 4, ic.y), ImVec2(ic.x + 4, ic.y), ic_col, 1.8f);
        dl->AddLine(ImVec2(ic.x - 1, ic.y + 5), ImVec2(ic.x + 1, ic.y + 5), ic_col, 1.8f);
    }
    {
        ImVec2 tp(p0.x + 34.0f, p0.y + (height - ImGui::GetTextLineHeight()) * 0.5f);
        dl->AddText(tp, ColF(t.text, 1.0f), current);
    }
    {
        ImVec2 ac(p1.x - 18.0f, p0.y + height * 0.5f);
        float a = s_anim * 3.1415926f;
        float ca = std::cos(a), sa = std::sin(a);
        auto rot = [&](float x, float y) { return ImVec2(ac.x + x * ca - y * sa, ac.y + x * sa + y * ca); };
        dl->AddTriangleFilled(rot(-6, -3), rot(6, -3), rot(0, 4), ColF(t.accent, 1.0f));
    }

    if (s_anim > 0.001f) {
        float a = s_anim;
        fg->PushClipRect(lp0, lp1, true);
        for (int i = 6; i >= 1; --i) {
            float e = (float)i * 1.4f;
            fg->AddRect(ImVec2(lp0.x - e, lp0.y - e), ImVec2(lp1.x + e, lp1.y + e),
                ColF(ImVec4(0, 0, 0, 1), 0.06f * a), 12.0f, 0, 1.0f);
        }
        ImU32 popup_bg = ColF(ImVec4(t.window_bg.x + 0.02f, t.window_bg.y + 0.02f, t.window_bg.z + 0.04f, 1.0f), 1.0f);
        ImU32 popup_bd = ColF(t.accent, 0.65f * a);
        fg->AddRectFilled(lp0, lp1, popup_bg, 10.0f);
        fg->AddRect(lp0, lp1, popup_bd, 10.0f, 0, 1.5f);

        float inner_x0 = lp0.x + 6.0f, inner_x1 = lp1.x - 6.0f, inner_y0 = lp0.y + pad_y;
        if (scroll) {
            float wheel = io.MouseWheel;
            if (wheel != 0.0f && ImGui::IsMouseHoveringRect(lp0, lp1)) s_scroll -= wheel * 40.0f;
            float max_scroll = full_h - list_h;
            if (s_scroll < 0.0f) s_scroll = 0.0f;
            if (s_scroll > max_scroll) s_scroll = max_scroll;
        }
        else s_scroll = 0.0f;

        int first = scroll ? (int)(s_scroll / row_h) : 0;
        int last = scroll ? std::min(n, first + (int)(list_h / row_h) + 2) : n;
        float y_off = scroll ? (s_scroll - first * row_h) : 0.0f;

        for (int i = first; i < last; ++i) {
            float ry0 = inner_y0 + (i - first) * row_h - y_off;
            float ry1 = ry0 + row_h;
            if (ry1 < lp0.y || ry0 > lp1.y) continue;
            ImVec2 r0(inner_x0, ry0), r1(inner_x1, ry1);
            bool item_hover = ImGui::IsMouseHoveringRect(r0, r1) && ImGui::IsMouseHoveringRect(lp0, lp1);
            bool selected = (i == g_app.selected_tag);
            if (selected) {
                fg->AddRectFilled(r0, r1, ColF(t.accent, (item_hover ? 0.45f : 0.30f) * a), 7.0f);
                fg->AddRectFilled(ImVec2(r0.x + 2, r0.y + 5), ImVec2(r0.x + 5, r1.y - 5), ColF(t.accent, 1.0f * a), 2.0f);
            }
            else if (item_hover) {
                fg->AddRectFilled(r0, r1, ColF(t.accent, 0.20f * a), 7.0f);
            }
            ImVec2 ts = ImGui::CalcTextSize(g_app.available_tags[i].c_str());
            ImVec2 tp(r0.x + 14.0f, r0.y + (row_h - ts.y) * 0.5f);
            ImU32 tcol = selected ? ColF(t.text, 1.0f) : item_hover ? ColF(t.text, 1.0f) : ColF(t.text, 0.85f);
            fg->AddText(tp, tcol, g_app.available_tags[i].c_str());
            if (selected) {
                ImVec2 cc(r1.x - 16.0f, r0.y + row_h * 0.5f);
                ImU32 chk = ColF(t.text, 1.0f);
                fg->AddLine(ImVec2(cc.x - 5, cc.y), ImVec2(cc.x - 2, cc.y + 3), chk, 2.0f);
                fg->AddLine(ImVec2(cc.x - 2, cc.y + 3), ImVec2(cc.x + 5, cc.y - 4), chk, 2.0f);
            }
            if (item_hover && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
                g_app.selected_tag = i;
                RebuildFiltered();
                s_open = false;
                g_tag_combo_just_picked = true;
            }
        }
        if (scroll) {
            float max_scroll = full_h - list_h;
            float bar_h = list_h * (list_h / full_h);
            float bar_y = lp0.y + (s_scroll / max_scroll) * (list_h - bar_h);
            fg->AddRectFilled(ImVec2(lp1.x - 5.0f, bar_y), ImVec2(lp1.x - 2.0f, bar_y + bar_h),
                ColF(t.accent, 0.65f * a), 2.0f);
        }
        fg->PopClipRect();
    }
    ImGui::PopID();
}

// ============================================================
//  Избранное — хелперы
// ============================================================
static bool IsFavorite(const std::string& appid) {
    for (auto& g : g_app.all_games) if (g.appid == appid) return g.favorite;
    return false;
}
static void ToggleFavorite(const std::string& appid) {
    for (auto& g : g_app.all_games) {
        if (g.appid == appid) {
            g.favorite = !g.favorite;
            if (g.favorite) {
                g_stat_fav++; SaveSettings();
                PushToast("Добавлено в избранное");
                AddXP(1, "избранное");
                if (g_stat_fav >= 1) UnlockAchievement("fav_first");
                if (g_stat_fav >= 10) UnlockAchievement("fav_ten");
                if (g_stat_fav >= 50) UnlockAchievement("fav_fifty");
            }
            else {
                PushToast("Удалено из избранного");
            }
            break;
        }
    }
    SaveSettings();
}

// ============================================================
//  Карточка
// ============================================================
static void DrawGameCard(int gi, ImVec2 p0, ImVec2 p1, bool blocked,
    bool& out_star, bool& out_copy)
{
    ThemeColors t = GetTheme();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    GameEntry& g = g_app.all_games[gi];
    float cw = p1.x - p0.x, ch = p1.y - p0.y;
    float dt = ImGui::GetIO().DeltaTime; if (dt <= 0.0f) dt = 0.016f;

    RequestImageLoad(g);
    bool hovered = ImGui::IsMouseHoveringRect(p0, p1) && !ImGui::IsAnyItemActive() && !blocked;
    if (hovered) {
        g_app.outline_any_hover = true;
        g_app.outline_pos_target = p0;
        g_app.outline_size_target = ImVec2(cw, ch);
    }
    g.star_hover_t = ExpLerp(g.star_hover_t, hovered ? 1.0f : 0.0f, 10.0f, dt);
    g.copy_hover_t = g.star_hover_t;

    dl->AddRectFilled(p0, p1, ColF(hovered ? t.card_bg_hover : t.card_bg, 1.0f), 10.0f);
    if (g.texture) {
        if (g.open_time > 0.0f) {
            float t2 = MY_CLAMP(((float)ImGui::GetTime() - g.open_time) * 4.0f, 0.0f, 1.0f);
            g.fade_in = EaseOutCubic(t2);
        }
        dl->AddImageRounded((ImTextureID)g.texture, p0, p1, ImVec2(0, 0), ImVec2(1, 1),
            ColF(ImVec4(1, 1, 1, 1), g.fade_in), 10.0f);
    }
    else {
        dl->AddRectFilled(p0, p1, IM_COL32(50, 48, 62, 255), 10.0f);
        const char* ni = "Нет изображения";
        ImVec2 sz = ImGui::CalcTextSize(ni);
        dl->AddText(ImVec2(p0.x + (cw - sz.x) * 0.5f, p0.y + (ch - sz.y) * 0.5f), IM_COL32(180, 175, 200, 255), ni);
    }

    if (g.installed) {
        const char* lbl = "Скачано";
        ImVec2 ts = ImGui::CalcTextSize(lbl);
        float px = 8.0f, py = 4.0f;
        float bw = ts.x + px * 2, bh = ts.y + py * 2;
        ImVec2 bp0(p0.x + 8, p0.y + 8);
        ImVec2 bp1(bp0.x + bw, bp0.y + bh);
        dl->AddRectFilled(bp0, bp1, IM_COL32(80, 200, 120, 255), bh * 0.5f);
        dl->AddText(ImVec2(bp0.x + px, bp0.y + py), IM_COL32(0, 0, 0, 255), lbl);
    }
    if (g.nsfw) {
        const char* lbl = "NSFW";
        ImVec2 ts = ImGui::CalcTextSize(lbl);
        float px = 6.0f, py = 3.0f;
        float bw = ts.x + px * 2, bh = ts.y + py * 2;
        ImVec2 bp0(p1.x - bw - 14, p0.y + 14);
        ImVec2 bp1(bp0.x + bw, bp0.y + bh);
        dl->AddRectFilled(bp0, bp1, IM_COL32(200, 60, 80, 200), bh * 0.5f);
        dl->AddText(ImVec2(bp0.x + px, bp0.y + py), IM_COL32(255, 255, 255, 230), lbl);
    }

    ImVec2 mp = ImGui::GetIO().MousePos;
    const float icon_sz = 34.0f;
    float star_x = p0.x + 8.0f, star_y = p1.y - icon_sz - 8.0f;
    float copy_x = p0.x + 8.0f, copy_y = p0.y + 8.0f;
    ImVec2 star0(star_x, star_y), star1(star_x + icon_sz, star_y + icon_sz);
    ImVec2 copy0(copy_x, copy_y), copy1(copy_x + icon_sz, copy_y + icon_sz);

    out_star = false;
    out_copy = false;

    if (g.star_hover_t > 0.01f) {
        float a = g.star_hover_t;
        bool sh = hovered && mp.x >= star0.x && mp.x <= star1.x && mp.y >= star0.y && mp.y <= star1.y;
        bool ch = hovered && mp.x >= copy0.x && mp.x <= copy1.x && mp.y >= copy0.y && mp.y <= copy1.y;

        if (ch && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) out_copy = true;
        else if (sh && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) out_star = true;

        ImVec2 so(0, (1.0f - a) * 8.0f), co(0, -(1.0f - a) * 8.0f);

        {
            ImVec2 c0(copy0.x + co.x, copy0.y + co.y), c1(copy1.x + co.x, copy1.y + co.y);
            dl->AddRectFilled(c0, c1, ColF(t.window_bg, 0.75f * a), 8.0f);
            dl->AddRect(c0, c1, ColF(t.accent, (ch ? 1.0f : 0.6f) * a), 8.0f, 0, 1.5f);
            ImU32 ic = ColF(t.text, a);
            dl->AddRect(ImVec2(c0.x + 10, c0.y + 8), ImVec2(c0.x + 22, c0.y + 20), ic, 2.0f, 0, 1.4f);
            dl->AddRect(ImVec2(c0.x + 8, c0.y + 12), ImVec2(c0.x + 20, c0.y + 24), ic, 2.0f, 0, 1.4f);
        }
        {
            ImVec2 s0(star0.x + so.x, star0.y + so.y), s1(star1.x + so.x, star1.y + so.y);
            dl->AddRectFilled(s0, s1, ColF(t.window_bg, 0.75f * a), 8.0f);
            dl->AddRect(s0, s1, ColF(t.accent, (sh ? 1.0f : 0.6f) * a), 8.0f, 0, 1.5f);
            ImVec2 c((s0.x + s1.x) * 0.5f, (s0.y + s1.y) * 0.5f);
            float R = 9.5f, r = R * 0.45f;
            ImU32 col = g.favorite ? IM_COL32(255, 210, 60, (int)(255 * a)) : IM_COL32(240, 240, 245, (int)(230 * a));
            ImU32 edge = g.favorite ? IM_COL32(255, 170, 30, (int)(255 * a)) : ColF(t.text, 0.85f * a);
            ImVec2 pts[10];
            for (int i = 0; i < 10; i++) {
                float ang = -1.5707963f + i * 3.1415926f / 5.0f;
                float rr = (i % 2 == 0) ? R : r;
                pts[i] = ImVec2(c.x + std::cos(ang) * rr, c.y + std::sin(ang) * rr);
            }
            dl->AddConvexPolyFilled(pts, 10, col);
            for (int i = 0; i < 10; i++) dl->AddLine(pts[i], pts[(i + 1) % 10], edge, 1.2f);
        }
    }

    bool over_star = hovered && mp.x >= star0.x && mp.x <= star1.x && mp.y >= star0.y && mp.y <= star1.y;
    bool over_copy = hovered && mp.x >= copy0.x && mp.x <= copy1.x && mp.y >= copy0.y && mp.y <= copy1.y;
    bool show_tt = hovered && !blocked && !over_star && !over_copy;
    if (show_tt) {
        ImDrawList* fg = ImGui::GetForegroundDrawList();
        ImVec2 ns = ImGui::CalcTextSize(g.name.c_str());
        char idb[64]; snprintf(idb, sizeof(idb), "AppID: %s", g.appid.c_str());
        ImVec2 ids = ImGui::CalcTextSize(idb);
        std::string tl = "Теги: ";
        if (g.tags.empty()) tl += "—";
        else for (size_t i = 0; i < g.tags.size(); ++i) { if (i) tl += ", "; tl += g.tags[i]; }
        if (g.nsfw) tl += "  [NSFW]";
        if (g.favorite) tl += "  [Избранное]";
        ImVec2 tls = ImGui::CalcTextSize(tl.c_str());
        float px = 12.0f, py = 10.0f;
        float bw = std::max({ ns.x, ids.x, tls.x }) + px * 2;
        float bh = ns.y + ids.y + tls.y + py * 2 + 10.0f;
        ImVec2 b0(mp.x + 16, mp.y + 16), b1(b0.x + bw, b0.y + bh);
        ImVec2 scr = ImGui::GetIO().DisplaySize;
        if (b1.x > scr.x - 10) b0.x = mp.x - bw - 16;
        if (b1.y > scr.y - 10) b0.y = mp.y - bh - 16;
        b1 = ImVec2(b0.x + bw, b0.y + bh);
        fg->AddRectFilled(b0, b1, IM_COL32(45, 45, 55, 250), 8.0f);
        fg->AddRect(b0, b1, ColF(t.accent, 0.7f), 8.0f, 0, 1.2f);
        fg->AddText(ImVec2(b0.x + px, b0.y + py), IM_COL32(255, 255, 255, 255), g.name.c_str());
        fg->AddText(ImVec2(b0.x + px, b0.y + py + ns.y + 4.0f), ColF(t.accent, 1.0f), idb);
        fg->AddText(ImVec2(b0.x + px, b0.y + py + ns.y + ids.y + 8.0f),
            g.nsfw ? IM_COL32(255, 140, 160, 255) : IM_COL32(200, 200, 210, 255), tl.c_str());
    }
}

// ============================================================
//  МАГАЗИН
// ============================================================
static void DrawShopPage() {
    ThemeColors t = GetTheme();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    float dt = ImGui::GetIO().DeltaTime; if (dt <= 0.0f) dt = 0.016f;

    ImGui::SetCursorPosX(ImGui::GetCursorPosX() + 24.0f);
    const float combo_w = 240.0f, combo_h = 38.0f;

    ImGui::BeginGroup();
    ImGui::SetNextItemWidth(360.0f);
    if (ImGui::InputTextWithHint("##search", "Поиск игры по названию или AppID...",
        g_app.search_buf, IM_ARRAYSIZE(g_app.search_buf))) {
        g_app.search_query = g_app.search_buf;
        g_app.random_mode = false;
        RebuildFiltered();
    }
    ImGui::EndGroup();

    float content_right = ImGui::GetWindowContentRegionMax().x - 24.0f;
    ImGui::SameLine();
    ImGui::SetCursorPosX(content_right - combo_w);
    DrawTagComboBox(combo_w, combo_h);

    ImGui::Spacing(); ImGui::Spacing();

    if (g_app.all_games.empty()) { ImGui::Dummy(ImVec2(0, 40)); ImGui::TextColored(t.text_disabled, "Загрузка базы игр..."); return; }
    if (g_app.filtered.empty()) { ImGui::Dummy(ImVec2(0, 40)); ImGui::TextColored(t.text_disabled, "По выбранному фильтру ничего не найдено."); return; }

    ImVec2 avail_region = ImGui::GetContentRegionAvail();
    float pager_h = 70.0f;
    float grid_h = avail_region.y - pager_h;
    float side_pad = 24.0f;
    avail_region.x -= side_pad * 2.0f;

    float gap = 14.0f;
    float card_w = (avail_region.x - gap * (AppState::kGridCols - 1)) / AppState::kGridCols;
    float card_h = (grid_h - gap * (AppState::kGridRows - 1)) / AppState::kGridRows;

    ImVec2 grid_origin = ImGui::GetCursorScreenPos();
    grid_origin.x += side_pad;

    bool busy = g_app.IsBusy();
    bool block = g_tag_combo_open || g_tag_combo_just_picked;

    if (g_app.random_mode && g_app.random_index >= 0 && g_app.random_index < (int)g_app.all_games.size()) {
        int gi = g_app.random_index;
        GameEntry& g = g_app.all_games[gi];
        ImVec2 p0 = grid_origin;
        ImVec2 p1 = ImVec2(p0.x + card_w, p0.y + card_h);
        bool s_c = false, c_c = false;
        DrawGameCard(gi, p0, p1, block, s_c, c_c);

        ImGui::SetCursorScreenPos(p0);
        ImGui::PushID(gi);
        ImGui::InvisibleButton("rand_card", ImVec2(card_w, card_h));
        bool card_clicked = ImGui::IsItemClicked();
        ImGui::PopID();

        bool st_c = false;
        if (!block) {
            ImVec2 mp = ImGui::GetIO().MousePos;
            const char* lbl = "Steam";
            ImVec2 ts = ImGui::CalcTextSize(lbl);
            float pad_x = 14.0f, pad_y = 7.0f;
            float bw = ts.x + pad_x * 2, bh = ts.y + pad_y * 2;
            ImVec2 sp1(p1.x - 10.0f, p1.y - 10.0f);
            ImVec2 sp0(sp1.x - bw, sp1.y - bh);
            bool sh = mp.x >= sp0.x && mp.x <= sp1.x && mp.y >= sp0.y && mp.y <= sp1.y;
            ImU32 bg = sh ? IM_COL32(30, 130, 220, 255) : IM_COL32(20, 90, 160, 235);
            ImU32 bd = sh ? IM_COL32(120, 200, 255, 255) : IM_COL32(80, 160, 230, 200);
            dl->AddRectFilled(sp0, sp1, bg, 8.0f);
            dl->AddRect(sp0, sp1, bd, 8.0f, 0, 1.5f);
            dl->AddText(ImVec2(sp0.x + pad_x, sp0.y + pad_y), IM_COL32(255, 255, 255, 255), lbl);
            if (sh && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) st_c = true;
        }

        if (s_c) ToggleFavorite(g.appid);
        else if (st_c) OpenGameInSteam(g.appid);
        else if (c_c) { CopyToClipboard(g.appid); g_stat_copy++; SaveSettings(); PushToast("AppID скопирован"); AddXP(1, "копирование"); UnlockAchievement("copy_id"); }
        else if (card_clicked && !busy && !block) DownloadManifestAsync(g.appid, g.name);
    }
    else {
        g_app.page_switch_t += dt * 5.5f;
        if (g_app.page_switch_t > 1.0f) g_app.page_switch_t = 1.0f;
        int start = g_app.page * AppState::kGamesPerPage;
        int end = std::min((int)g_app.filtered.size(), start + AppState::kGamesPerPage);
        g_app.outline_any_hover = false;

        for (int idx = start; idx < end; idx++) {
            int gi = g_app.filtered[idx];
            int local = idx - start;
            int col = local % AppState::kGridCols, row = local / AppState::kGridCols;
            ImVec2 p0(grid_origin.x + col * (card_w + gap), grid_origin.y + row * (card_h + gap));
            ImVec2 p1(p0.x + card_w, p0.y + card_h);

            bool s_c = false, c_c = false;
            DrawGameCard(gi, p0, p1, block, s_c, c_c);

            ImGui::PushID(gi);
            ImGui::SetCursorScreenPos(p0);
            ImGui::InvisibleButton("card", ImVec2(card_w, card_h));
            bool card_clicked = ImGui::IsItemClicked();
            ImGui::PopID();

            bool st_c = false;
            if (!block) {
                ImVec2 mp = ImGui::GetIO().MousePos;
                const char* lbl = "Steam";
                ImVec2 ts = ImGui::CalcTextSize(lbl);
                float pad_x = 14.0f, pad_y = 7.0f;
                float bw = ts.x + pad_x * 2, bh = ts.y + pad_y * 2;
                ImVec2 sp1(p1.x - 10.0f, p1.y - 10.0f);
                ImVec2 sp0(sp1.x - bw, sp1.y - bh);
                bool sh = mp.x >= sp0.x && mp.x <= sp1.x && mp.y >= sp0.y && mp.y <= sp1.y;
                ImU32 bg = sh ? IM_COL32(30, 130, 220, 255) : IM_COL32(20, 90, 160, 235);
                ImU32 bd = sh ? IM_COL32(120, 200, 255, 255) : IM_COL32(80, 160, 230, 200);
                dl->AddRectFilled(sp0, sp1, bg, 8.0f);
                dl->AddRect(sp0, sp1, bd, 8.0f, 0, 1.5f);
                dl->AddText(ImVec2(sp0.x + pad_x, sp0.y + pad_y), IM_COL32(255, 255, 255, 255), lbl);
                if (sh && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) st_c = true;
            }

            if (s_c) ToggleFavorite(g_app.all_games[gi].appid);
            else if (st_c) OpenGameInSteam(g_app.all_games[gi].appid);
            else if (c_c) {
                CopyToClipboard(g_app.all_games[gi].appid);
                g_stat_copy++; SaveSettings();
                PushToast("AppID скопирован");
                AddXP(1, "копирование");
                UnlockAchievement("copy_id");
                if (g_stat_copy >= 100) UnlockAchievement("copy_hundred");
            }
            else if (card_clicked && !busy && !block)
                DownloadManifestAsync(g_app.all_games[gi].appid, g_app.all_games[gi].name);
        }

        g_app.outline_alpha = ExpLerp(g_app.outline_alpha, g_app.outline_any_hover ? 1.0f : 0.0f, 10.0f, dt);
        if (g_app.outline_alpha > 0.01f) {
            if (g_app.outline_size_cur.x <= 1.0f) { g_app.outline_pos_cur = g_app.outline_pos_target; g_app.outline_size_cur = g_app.outline_size_target; }
            g_app.outline_pos_cur = ExpLerp2(g_app.outline_pos_cur, g_app.outline_pos_target, 18.0f, dt);
            g_app.outline_size_cur = ExpLerp2(g_app.outline_size_cur, g_app.outline_size_target, 18.0f, dt);
            ImVec2 o0 = g_app.outline_pos_cur, o1(o0.x + g_app.outline_size_cur.x, o0.y + g_app.outline_size_cur.y);
            for (int i = 6; i >= 1; i--) {
                float e = (float)i * 1.6f;
                dl->AddRect(ImVec2(o0.x - e, o0.y - e), ImVec2(o1.x + e, o1.y + e),
                    ColF(t.accent, g_app.outline_alpha * (0.06f / i)), 12.0f + e, 0, 2.0f);
            }
            dl->AddRect(o0, o1, ColF(t.accent, g_app.outline_alpha), 12.0f, 0, 2.5f);
        }
    }

    // Пагинация
    int total_pages = std::max(1, (int)((g_app.filtered.size() + AppState::kGamesPerPage - 1) / AppState::kGamesPerPage));
    float pager_y = grid_origin.y + grid_h + 6.0f;
    float pager_c = pager_y + pager_h * 0.5f;
    ImDrawList* pdl = ImGui::GetWindowDrawList();
    ImVec2 mp = ImGui::GetIO().MousePos;
    ImVec2 win_size = ImGui::GetWindowSize(), win_pos = ImGui::GetWindowPos();
    float right_edge = win_pos.x + win_size.x - 30.0f;

    const float sq_mid = 50.0f, sq_side = 42.0f, sq_gap = 14.0f;
    const float arrow_w = 30.0f, arrow_h = 32.0f, arrow_gap = 22.0f;
    float group_center = grid_origin.x + avail_region.x * 0.5f;
    float mid_x = group_center - sq_mid * 0.5f;

    char pg_buf[64]; snprintf(pg_buf, sizeof(pg_buf), "Страниц: %d", total_pages);
    ImVec2 pg_sz = ImGui::CalcTextSize(pg_buf);
    pdl->AddText(ImVec2(grid_origin.x, pager_c - pg_sz.y * 0.5f), IM_COL32(140, 138, 155, 255), pg_buf);

    // Случайная игра
    {
        const char* lbl = "Случайная игра";
        ImVec2 ts = ImGui::CalcTextSize(lbl);
        float bw = ts.x + 40.0f, bh = 40.0f;
        float bx = grid_origin.x + pg_sz.x + 30.0f;
        float byy = pager_y + (pager_h - bh) * 0.5f;
        ImVec2 b0(bx, byy), b1(bx + bw, byy + bh);
        bool hv = mp.x >= b0.x && mp.x <= b1.x && mp.y >= b0.y && mp.y <= b1.y;
        ImU32 bg = hv ? ColF(t.accent, 0.35f) : ColF(t.accent, 0.15f);
        pdl->AddRectFilled(b0, b1, bg, 10.0f);
        pdl->AddRect(b0, b1, ColF(t.accent, hv ? 1.0f : 0.5f), 10.0f, 0, 1.5f);
        pdl->AddText(ImVec2(b0.x + 20, b0.y + (bh - ts.y) * 0.5f), ColF(t.text, 1.0f), lbl);
        if (hv && ImGui::IsMouseClicked(ImGuiMouseButton_Left) && !busy && !block && !g_app.filtered.empty()) {
            std::mt19937 rng((unsigned)std::chrono::system_clock::now().time_since_epoch().count());
            std::uniform_int_distribution<int> dist(0, (int)g_app.filtered.size() - 1);
            g_app.random_index = g_app.filtered[dist(rng)];
            g_app.random_mode = true;
            g_stat_random++; SaveSettings();
            PushToast("Случайная игра выбрана");
            AddXP(1, "рандом");
            UnlockAchievement("random_use");
            if (g_stat_random >= 10) UnlockAchievement("random_ten");
        }
    }

    if (!g_app.random_mode) {
        {
            float c_x = mid_x - sq_gap - sq_side - arrow_gap - arrow_w * 0.5f;
            ImVec2 a0(c_x - arrow_w * 0.5f, pager_c - arrow_h * 0.5f), a1(c_x + arrow_w * 0.5f, pager_c + arrow_h * 0.5f);
            bool en = (g_app.page > 0) && !busy && !block;
            bool hv = en && mp.x >= a0.x && mp.x <= a1.x && mp.y >= a0.y && mp.y <= a1.y;
            ImU32 col = !en ? IM_COL32(120, 120, 130, 90) : hv ? ColF(t.accent, 1.0f) : IM_COL32(230, 230, 240, 230);
            float tip_x = c_x - arrow_w * 0.35f, base_x = c_x + arrow_w * 0.35f;
            pdl->AddTriangleFilled(ImVec2(tip_x, pager_c), ImVec2(base_x, pager_c - 11), ImVec2(base_x, pager_c + 11), col);
            pdl->AddRectFilled(ImVec2(c_x - 2, pager_c - 3), ImVec2(base_x, pager_c + 3), col);
            if (hv && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) { g_app.prev_page = g_app.page; g_app.page--; g_app.page_switch_t = 0.0f; }
        }
        {
            int lp = g_app.page - 1, rp = g_app.page + 1;
            auto draw_sq = [&](int pn, float x, bool is_cur) {
                float sq = is_cur ? sq_mid : sq_side;
                ImVec2 s0(x, pager_y + (pager_h - sq) * 0.5f), s1(s0.x + sq, s0.y + sq);
                bool hv = mp.x >= s0.x && mp.x <= s1.x && mp.y >= s0.y && mp.y <= s1.y;
                if (!is_cur) hv = hv && !busy && !block;
                char b[16];
                if (is_cur) {
                    for (int i = 5; i >= 1; i--) {
                        float e = i * 1.4f;
                        pdl->AddRect(ImVec2(s0.x - e, s0.y - e), ImVec2(s1.x + e, s1.y + e), ColF(t.accent, 0.10f / i), 12.0f, 0, 1.5f);
                    }
                    pdl->AddRectFilled(s0, s1, IM_COL32(255, 255, 255, 15), 10.0f);
                    pdl->AddRect(s0, s1, ColF(t.accent, 1.0f), 10.0f, 0, 2.5f);
                    snprintf(b, sizeof(b), "%d", pn);
                    ImVec2 ts = ImGui::CalcTextSize(b);
                    pdl->AddText(ImVec2(s0.x + (sq - ts.x) * 0.5f, s0.y + (sq - ts.y) * 0.5f), ColF(t.text, 1.0f), b);
                }
                else {
                    snprintf(b, sizeof(b), "%d", pn);
                    ImVec2 ts = ImGui::CalcTextSize(b);
                    ImU32 bd = hv ? ColF(t.accent, 0.8f) : IM_COL32(180, 180, 190, 200);
                    pdl->AddRect(s0, s1, bd, 10.0f, 0, 1.8f);
                    pdl->AddText(ImVec2(s0.x + (sq - ts.x) * 0.5f, s0.y + (sq - ts.y) * 0.5f),
                        hv ? ColF(t.accent, 1.0f) : IM_COL32(200, 200, 210, 220), b);
                    if (hv && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) { g_app.prev_page = g_app.page; g_app.page = pn - 1; g_app.page_switch_t = 0.0f; }
                }
                };
            if (lp >= 0) draw_sq(lp + 1, mid_x - sq_gap - sq_side, false);
            draw_sq(g_app.page + 1, mid_x, true);
            if (rp < total_pages) draw_sq(rp + 1, mid_x + sq_mid + sq_gap, false);
        }
        {
            int rp = g_app.page + 1;
            float re = (rp >= total_pages) ? (mid_x + sq_mid) : (mid_x + sq_mid + sq_gap + sq_side);
            float c_x = re + arrow_gap + arrow_w * 0.5f;
            ImVec2 a0(c_x - arrow_w * 0.5f, pager_c - arrow_h * 0.5f), a1(c_x + arrow_w * 0.5f, pager_c + arrow_h * 0.5f);
            bool en = (g_app.page < total_pages - 1) && !busy && !block;
            bool hv = en && mp.x >= a0.x && mp.x <= a1.x && mp.y >= a0.y && mp.y <= a1.y;
            ImU32 col = !en ? IM_COL32(120, 120, 130, 90) : hv ? ColF(t.accent, 1.0f) : IM_COL32(230, 230, 240, 230);
            float tip_x = c_x + arrow_w * 0.35f, base_x = c_x - arrow_w * 0.35f;
            pdl->AddTriangleFilled(ImVec2(tip_x, pager_c), ImVec2(base_x, pager_c - 11), ImVec2(base_x, pager_c + 11), col);
            pdl->AddRectFilled(ImVec2(base_x, pager_c - 3), ImVec2(c_x + 2, pager_c + 3), col);
            if (hv && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) { g_app.prev_page = g_app.page; g_app.page++; g_app.page_switch_t = 0.0f; }
        }
        {
            const float inp_w = 200.0f, inp_h = 42.0f;
            ImVec2 i0(right_edge - inp_w, pager_y + (pager_h - inp_h) * 0.5f);
            ImGui::SetCursorScreenPos(i0);
            ImGui::PushID("goto_page");
            bool err = (g_app.goto_error_timer > 0.0f);
            if (err) { ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(1, 0.25f, 0.30f, 1)); ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 2.5f); }
            else { ImGui::PushStyleColor(ImGuiCol_Border, ColF(t.accent, 0.8f)); ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 1.5f); }
            ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 21.0f);
            ImGui::PushStyleColor(ImGuiCol_FrameBg, IM_COL32(255, 255, 255, 8));
            ImGui::SetNextItemWidth(inp_w);
            if (ImGui::InputTextWithHint("##goto", "К странице...", g_app.goto_buf, IM_ARRAYSIZE(g_app.goto_buf),
                ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_CharsDecimal)) {
                int v = atoi(g_app.goto_buf);
                if (v >= 1 && v <= total_pages && !busy) {
                    g_app.prev_page = g_app.page; g_app.page = v - 1;
                    g_app.page_switch_t = 0.0f; g_app.goto_buf[0] = 0; g_app.goto_error_timer = 0.0f;
                }
                else g_app.goto_error_timer = 0.6f;
            }
            ImGui::PopStyleColor(2); ImGui::PopStyleVar(2);
            if (g_app.goto_error_timer > 0.0f) { g_app.goto_error_timer -= dt; if (g_app.goto_error_timer < 0.0f) g_app.goto_error_timer = 0.0f; }
            ImGui::PopID();
        }
    }
    else {
        const char* lbl = "Сбросить";
        ImVec2 ts = ImGui::CalcTextSize(lbl);
        float bw = ts.x + 30.0f, bh = 34.0f;
        float bx = group_center - bw * 0.5f;
        float byy = pager_y + (pager_h - bh) * 0.5f;
        ImVec2 b0(bx, byy), b1(bx + bw, byy + bh);
        bool hv = mp.x >= b0.x && mp.x <= b1.x && mp.y >= b0.y && mp.y <= b1.y;
        ImU32 bg = hv ? ColF(t.accent2, 0.35f) : ColF(t.accent2, 0.15f);
        pdl->AddRectFilled(b0, b1, bg, 8.0f);
        pdl->AddRect(b0, b1, ColF(t.accent2, hv ? 1.0f : 0.5f), 8.0f, 0, 1.5f);
        pdl->AddText(ImVec2(b0.x + 15, b0.y + (bh - ts.y) * 0.5f), ColF(t.text, 1.0f), lbl);
        if (hv && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) { g_app.random_mode = false; g_app.random_index = -1; }
    }
}

// ============================================================
//  ИЗБРАННОЕ
// ============================================================
static void DrawFavoritesPage() {
    ThemeColors t = GetTheme();
    bool busy = g_app.IsBusy();
    bool block = g_tag_combo_open || g_tag_combo_just_picked;

    std::vector<int> favs;
    for (int i = 0; i < (int)g_app.all_games.size(); ++i)
        if (g_app.all_games[i].favorite) favs.push_back(i);

    ImGui::Dummy(ImVec2(0, 4));
    ImGui::SetCursorPosX(ImGui::GetCursorPosX() + 24.0f);
    ImGui::TextDisabled("Избранных игр: %d", (int)favs.size());
    ImGui::Spacing();

    if (favs.empty()) {
        ImGui::Dummy(ImVec2(0, 40));
        ImGui::TextColored(t.text_disabled, "В избранном пока пусто. Нажмите звезду на карточке игры.");
        return;
    }

    ImVec2 avail = ImGui::GetContentRegionAvail();
    float pager_h = 70.0f;
    float grid_h = avail.y - pager_h;
    float side_pad = 24.0f;
    avail.x -= side_pad * 2.0f;
    float gap = 14.0f;
    float card_w = (avail.x - gap * (AppState::kGridCols - 1)) / AppState::kGridCols;
    float card_h = (grid_h - gap * (AppState::kGridRows - 1)) / AppState::kGridRows;

    ImVec2 grid_origin = ImGui::GetCursorScreenPos();
    grid_origin.x += side_pad;

    int total_pages = std::max(1, (int)((favs.size() + AppState::kGamesPerPage - 1) / AppState::kGamesPerPage));
    if (g_app.fav_page >= total_pages) g_app.fav_page = total_pages - 1;
    if (g_app.fav_page < 0) g_app.fav_page = 0;

    int start = g_app.fav_page * AppState::kGamesPerPage;
    int end = std::min((int)favs.size(), start + AppState::kGamesPerPage);

    g_app.outline_any_hover = false;
    ImDrawList* dl = ImGui::GetWindowDrawList();

    for (int idx = start; idx < end; ++idx) {
        int gi = favs[idx];
        int local = idx - start, col = local % AppState::kGridCols, row = local / AppState::kGridCols;
        ImVec2 p0(grid_origin.x + col * (card_w + gap), grid_origin.y + row * (card_h + gap));
        ImVec2 p1(p0.x + card_w, p0.y + card_h);

        bool s_c = false, c_c = false;
        DrawGameCard(gi, p0, p1, block, s_c, c_c);

        ImGui::PushID(gi);
        ImGui::SetCursorScreenPos(p0);
        ImGui::InvisibleButton("card_fav", ImVec2(card_w, card_h));
        bool cc = ImGui::IsItemClicked();
        ImGui::PopID();

        bool st_c = false;
        if (!block) {
            ImVec2 mp = ImGui::GetIO().MousePos;
            const char* lbl = "Steam";
            ImVec2 ts = ImGui::CalcTextSize(lbl);
            float pad_x = 14.0f, pad_y = 7.0f;
            float bw = ts.x + pad_x * 2, bh = ts.y + pad_y * 2;
            ImVec2 sp1(p1.x - 10.0f, p1.y - 10.0f);
            ImVec2 sp0(sp1.x - bw, sp1.y - bh);
            bool sh = mp.x >= sp0.x && mp.x <= sp1.x && mp.y >= sp0.y && mp.y <= sp1.y;
            ImU32 bg = sh ? IM_COL32(30, 130, 220, 255) : IM_COL32(20, 90, 160, 235);
            ImU32 bd = sh ? IM_COL32(120, 200, 255, 255) : IM_COL32(80, 160, 230, 200);
            dl->AddRectFilled(sp0, sp1, bg, 8.0f);
            dl->AddRect(sp0, sp1, bd, 8.0f, 0, 1.5f);
            dl->AddText(ImVec2(sp0.x + pad_x, sp0.y + pad_y), IM_COL32(255, 255, 255, 255), lbl);
            if (sh && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) st_c = true;
        }

        if (s_c) ToggleFavorite(g_app.all_games[gi].appid);
        else if (st_c) OpenGameInSteam(g_app.all_games[gi].appid);
        else if (c_c) { CopyToClipboard(g_app.all_games[gi].appid); g_stat_copy++; SaveSettings(); PushToast("AppID скопирован"); AddXP(1, "копирование"); UnlockAchievement("copy_id"); }
        else if (cc && !busy && !block) DownloadManifestAsync(g_app.all_games[gi].appid, g_app.all_games[gi].name);
    }

    float pager_y = grid_origin.y + grid_h + 6.0f;
    float pager_c = pager_y + pager_h * 0.5f;
    ImDrawList* pdl = ImGui::GetWindowDrawList();
    ImVec2 mp = ImGui::GetIO().MousePos;
    const float sq_mid = 50.0f, sq_side = 42.0f, sq_gap = 14.0f, arrow_w = 30.0f, arrow_h = 32.0f, arrow_gap = 22.0f;
    float group_center = grid_origin.x + avail.x * 0.5f;
    float mid_x = group_center - sq_mid * 0.5f;

    {
        float c_x = mid_x - sq_gap - sq_side - arrow_gap - arrow_w * 0.5f;
        ImVec2 a0(c_x - arrow_w * 0.5f, pager_c - arrow_h * 0.5f), a1(c_x + arrow_w * 0.5f, pager_c + arrow_h * 0.5f);
        bool en = (g_app.fav_page > 0) && !busy && !block;
        bool hv = en && mp.x >= a0.x && mp.x <= a1.x && mp.y >= a0.y && mp.y <= a1.y;
        ImU32 col = !en ? IM_COL32(120, 120, 130, 90) : hv ? ColF(t.accent, 1.0f) : IM_COL32(230, 230, 240, 230);
        pdl->AddTriangleFilled(ImVec2(c_x - arrow_w * 0.35f, pager_c), ImVec2(c_x + arrow_w * 0.35f, pager_c - 11), ImVec2(c_x + arrow_w * 0.35f, pager_c + 11), col);
        pdl->AddRectFilled(ImVec2(c_x - 2, pager_c - 3), ImVec2(c_x + arrow_w * 0.35f, pager_c + 3), col);
        if (hv && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) g_app.fav_page--;
    }
    {
        int lp = g_app.fav_page - 1, rp = g_app.fav_page + 1;
        auto draw_sq = [&](int pn, float x, bool is_cur) {
            float sq = is_cur ? sq_mid : sq_side;
            ImVec2 s0(x, pager_y + (pager_h - sq) * 0.5f), s1(s0.x + sq, s0.y + sq);
            bool hv = mp.x >= s0.x && mp.x <= s1.x && mp.y >= s0.y && mp.y <= s1.y;
            if (!is_cur) hv = hv && !busy && !block;
            char b[16];
            if (is_cur) {
                for (int i = 5; i >= 1; i--) {
                    float e = i * 1.4f;
                    pdl->AddRect(ImVec2(s0.x - e, s0.y - e), ImVec2(s1.x + e, s1.y + e), ColF(t.accent, 0.10f / i), 12.0f, 0, 1.5f);
                }
                pdl->AddRectFilled(s0, s1, IM_COL32(255, 255, 255, 15), 10.0f);
                pdl->AddRect(s0, s1, ColF(t.accent, 1.0f), 10.0f, 0, 2.5f);
                snprintf(b, sizeof(b), "%d", pn);
                ImVec2 ts = ImGui::CalcTextSize(b);
                pdl->AddText(ImVec2(s0.x + (sq - ts.x) * 0.5f, s0.y + (sq - ts.y) * 0.5f), ColF(t.text, 1.0f), b);
            }
            else {
                snprintf(b, sizeof(b), "%d", pn);
                ImVec2 ts = ImGui::CalcTextSize(b);
                ImU32 bd = hv ? ColF(t.accent, 0.8f) : IM_COL32(180, 180, 190, 200);
                pdl->AddRect(s0, s1, bd, 10.0f, 0, 1.8f);
                pdl->AddText(ImVec2(s0.x + (sq - ts.x) * 0.5f, s0.y + (sq - ts.y) * 0.5f),
                    hv ? ColF(t.accent, 1.0f) : IM_COL32(200, 200, 210, 220), b);
                if (hv && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) g_app.fav_page = pn - 1;
            }
            };
        if (lp >= 0) draw_sq(lp + 1, mid_x - sq_gap - sq_side, false);
        draw_sq(g_app.fav_page + 1, mid_x, true);
        if (rp < total_pages) draw_sq(rp + 1, mid_x + sq_mid + sq_gap, false);
    }
    {
        int rp = g_app.fav_page + 1;
        float re = (rp >= total_pages) ? (mid_x + sq_mid) : (mid_x + sq_mid + sq_gap + sq_side);
        float c_x = re + arrow_gap + arrow_w * 0.5f;
        ImVec2 a0(c_x - arrow_w * 0.5f, pager_c - arrow_h * 0.5f), a1(c_x + arrow_w * 0.5f, pager_c + arrow_h * 0.5f);
        bool en = (g_app.fav_page < total_pages - 1) && !busy && !block;
        bool hv = en && mp.x >= a0.x && mp.x <= a1.x && mp.y >= a0.y && mp.y <= a1.y;
        ImU32 col = !en ? IM_COL32(120, 120, 130, 90) : hv ? ColF(t.accent, 1.0f) : IM_COL32(230, 230, 240, 230);
        pdl->AddTriangleFilled(ImVec2(c_x + arrow_w * 0.35f, pager_c), ImVec2(c_x - arrow_w * 0.35f, pager_c - 11), ImVec2(c_x - arrow_w * 0.35f, pager_c + 11), col);
        pdl->AddRectFilled(ImVec2(c_x - arrow_w * 0.35f, pager_c - 3), ImVec2(c_x + 2, pager_c + 3), col);
        if (hv && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) g_app.fav_page++;
    }

    char b[64]; snprintf(b, sizeof(b), "Страниц: %d", total_pages);
    pdl->AddText(ImVec2(grid_origin.x, pager_c - 8.0f), IM_COL32(140, 138, 155, 255), b);
}

// ============================================================
//  ДОСТИЖЕНИЯ
// ============================================================
static void DrawAchievementsPage() {
    ThemeColors t = GetTheme();
    ImGui::Dummy(ImVec2(0, 4));
    ImGui::SetCursorPosX(ImGui::GetCursorPosX() + 24.0f);
    ImGui::Spacing();

    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
    ImGui::BeginChild("##ach_scroll", ImVec2(0, 0), false, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
    ImGui::PopStyleVar();

    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImVec2 wp = ImGui::GetWindowPos();
    ImVec2 ws = ImGui::GetWindowSize();

    float row_h = 52.0f, gap = 8.0f;
    float pad_x = 24.0f;
    float list_top = wp.y + 8.0f;
    float list_bottom = wp.y + ws.y - 8.0f;
    float list_h = list_bottom - list_top;
    float row_w = ws.x - pad_x * 2.0f - 22.0f;

    float content_h = g_achievements.size() * (row_h + gap);
    static float s_scroll = 0.0f;
    float max_scroll = std::max(0.0f, content_h - list_h);
    ImVec2 mp = ImGui::GetIO().MousePos;
    bool over = mp.x >= wp.x && mp.x <= wp.x + ws.x && mp.y >= list_top && mp.y <= list_bottom;
    if (over && ImGui::GetIO().MouseWheel != 0.0f) {
        s_scroll -= ImGui::GetIO().MouseWheel * 60.0f;
        if (s_scroll < 0.0f) s_scroll = 0.0f;
        if (s_scroll > max_scroll) s_scroll = max_scroll;
    }

    int first = (int)(s_scroll / (row_h + gap));
    int visible = (int)(list_h / (row_h + gap)) + 2;
    int last = std::min((int)g_achievements.size(), first + visible);
    float y_off = s_scroll - first * (row_h + gap);

    // Редкости
    auto rarityColor = [&](int r) -> ImU32 {
        switch (r) {
        case 0: return IM_COL32(160, 160, 180, 255);
        case 1: return IM_COL32(80, 180, 255, 255);
        case 2: return IM_COL32(180, 100, 255, 255);
        case 3: return IM_COL32(255, 180, 60, 255);
        default: return IM_COL32(160, 160, 180, 255);
        }
        };
    auto rarityName = [](int r) -> const char* {
    switch (r) { case 0: return "Обычное"; case 1: return "Редкое"; case 2: return "Эпическое"; case 3: return "Легендарное"; default: return ""; }
        };

    for (int i = first; i < last; ++i) {
        auto& a = g_achievements[i];
        float y = list_top + (i - first) * (row_h + gap) - y_off;
        if (y + row_h < list_top || y > list_bottom) continue;
        ImVec2 p0(wp.x + pad_x, y);
        ImVec2 p1(wp.x + pad_x + row_w, y + row_h);

        ImU32 rc = rarityColor(a.rarity);
        ImU32 bg = a.done ? (a.rarity == 3 ? IM_COL32(255, 180, 60, 30) : ColF(t.accent, 0.12f)) : IM_COL32(255, 255, 255, 10);
        dl->AddRectFilled(p0, p1, bg, 10.0f);
        dl->AddRect(p0, p1, a.done ? rc : IM_COL32(120, 120, 130, 140), 10.0f, 0, a.rarity >= 2 && a.done ? 2.0f : 1.5f);

        float sq = 28.0f;
        ImVec2 s0(p1.x - sq - 14, p0.y + (row_h - sq) * 0.5f);
        ImVec2 s1(s0.x + sq, s0.y + sq);
        if (a.done) {
            dl->AddRectFilled(s0, s1, IM_COL32(60, 190, 100, 255), 6.0f);
            ImVec2 c((s0.x + s1.x) * 0.5f, (s0.y + s1.y) * 0.5f);
            dl->AddLine(ImVec2(c.x - 8, c.y), ImVec2(c.x - 2, c.y + 6), IM_COL32(255, 255, 255, 255), 2.6f);
            dl->AddLine(ImVec2(c.x - 2, c.y + 6), ImVec2(c.x + 9, c.y - 7), IM_COL32(255, 255, 255, 255), 2.6f);
        }
        else {
            dl->AddRectFilled(s0, s1, IM_COL32(80, 80, 90, 180), 6.0f);
            dl->AddRect(s0, s1, IM_COL32(140, 140, 150, 200), 6.0f, 0, 1.5f);
        }

        // Скрытое до получения
        std::string title = a.title;
        std::string desc = a.desc;
        if (a.secret && !a.done) { title = "??? — Секрет"; desc = "Секретное достижение"; }

        dl->AddText(ImVec2(p0.x + 18, p0.y + 8), a.done ? rc : ColF(t.text, 1.0f), title.c_str());
        dl->AddText(ImVec2(p0.x + 18, p0.y + 28), ColF(t.text_disabled, 1.0f), desc.c_str());

        // Редкость слева
        dl->AddText(ImVec2(p0.x - 22, p0.y + 6), rc, "");
        // Редкость в правом верхнем
        ImVec2 rn_sz = ImGui::CalcTextSize(rarityName(a.rarity));
        dl->AddText(ImVec2(p1.x - rn_sz.x - 50, p0.y + 8), rc, rarityName(a.rarity));
    }

    if (max_scroll > 0.0f) {
        float track_x = wp.x + ws.x - 14.0f;
        float track_w = 6.0f;
        float bar_h = std::max(40.0f, list_h * (list_h / content_h));
        float bar_y = list_top + (s_scroll / max_scroll) * (list_h - bar_h);
        dl->AddRectFilled(ImVec2(track_x, list_top), ImVec2(track_x + track_w, list_bottom),
            IM_COL32(255, 255, 255, 15), 3.0f);
        dl->AddRectFilled(ImVec2(track_x, bar_y), ImVec2(track_x + track_w, bar_y + bar_h),
            ColF(t.accent, 0.7f), 3.0f);
    }

    ImGui::EndChild();
}

// ============================================================
//  МИНИ-ИГРЫ (UI)
// ============================================================
static void DrawStarsInline(ImDrawList* dl, ImVec2 pos, int stars, float size) {
    for (int i = 0; i < 5; i++) {
        ImVec2 c(pos.x + i * (size + 3), pos.y);
        float R = size * 0.5f, r = R * 0.45f;
        ImU32 col = (i < stars) ? IM_COL32(255, 210, 60, 255) : IM_COL32(100, 100, 110, 180);
        ImVec2 pts[10];
        for (int k = 0; k < 10; k++) {
            float ang = -1.5707963f + k * 3.1415926f / 5.0f;
            float rr = (k % 2 == 0) ? R : r;
            pts[k] = ImVec2(c.x + std::cos(ang) * rr, c.y + std::sin(ang) * rr);
        }
        dl->AddConvexPolyFilled(pts, 10, col);
    }
}

static void DrawMiniGameReaction(ImVec2 p0, ImVec2 p1) {
    ThemeColors t = GetTheme();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImVec2 mp = ImGui::GetIO().MousePos;
    float dt = ImGui::GetIO().DeltaTime;

    ImVec2 c((p0.x + p1.x) * 0.5f, (p0.y + p1.y) * 0.5f);

    if (g_mini.running) {
        if (!g_mini.react_ready) {
            // Ждём
            g_mini.timer += dt;
            dl->AddRectFilled(p0, p1, IM_COL32(60, 40, 80, 200), 10.0f);
            const char* txt = "Ждите...";
            ImVec2 ts = ImGui::CalcTextSize(txt);
            dl->AddText(ImVec2(c.x - ts.x * 0.5f, c.y - ts.y * 0.5f), ColF(t.text, 1.0f), txt);
            if (g_mini.timer > 1.5f + (rand() % 2000) / 1000.0f) {
                g_mini.react_ready = true;
                g_mini.react_start = (float)ImGui::GetTime();
            }
        }
        else {
            // Зелёный — жми
            float pulse = 0.5f + 0.5f * std::sin((float)ImGui::GetTime() * 6.0f);
            dl->AddRectFilled(p0, p1, IM_COL32(60 + (int)(pulse * 60), 180, 80, 220), 10.0f);
            const char* txt = "ЖМИ!";
            ImVec2 ts = ImGui::CalcTextSize(txt);
            dl->AddText(ImVec2(c.x - ts.x * 0.5f, c.y - ts.y * 0.5f), IM_COL32(255, 255, 255, 255), txt);

            if (ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
                float elapsed = ((float)ImGui::GetTime() - g_mini.react_start) * 1000.0f;
                g_mini.score = (int)elapsed;
                MiniFinish();
            }
            if (g_mini.timer > 5.0f) { g_mini.score = 9999; MiniFinish(); }
        }
    }
    else if (g_mini.finished) {
        dl->AddRectFilled(p0, p1, IM_COL32(40, 30, 50, 220), 10.0f);
        ImVec2 ts = ImGui::CalcTextSize(g_mini.message.c_str());
        dl->AddText(ImVec2(c.x - ts.x * 0.5f, c.y - ts.y * 0.5f), ColF(t.text, 1.0f), g_mini.message.c_str());
    }
}

static void DrawMiniGameMemory(ImVec2 p0, ImVec2 p1) {
    ThemeColors t = GetTheme();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImVec2 mp = ImGui::GetIO().MousePos;
    float dt = ImGui::GetIO().DeltaTime;

    if (!g_mini.running) {
        if (g_mini.finished) {
            ImVec2 c((p0.x + p1.x) * 0.5f, (p0.y + p1.y) * 0.5f);
            ImVec2 ts = ImGui::CalcTextSize(g_mini.message.c_str());
            dl->AddText(ImVec2(c.x - ts.x * 0.5f, c.y - ts.y * 0.5f), ColF(t.text, 1.0f), g_mini.message.c_str());
        }
        return;
    }

    int total = (int)g_mini.mem_cards.size();
    int cols = 5;
    int rows = (total + cols - 1) / cols;
    float cw = (p1.x - p0.x - 20.0f) / cols - 8.0f;
    float chh = (p1.y - p0.y - 20.0f) / rows - 8.0f;
    cw = std::min(cw, chh);

    if (g_mini.mem_flash_t > 0.0f) {
        g_mini.mem_flash_t -= dt;
        if (g_mini.mem_flash_t <= 0.0f) {
            g_mini.mem_flipped[g_mini.mem_flip_a] = false;
            g_mini.mem_flipped[g_mini.mem_flip_b] = false;
            g_mini.mem_flip_a = -1;
            g_mini.mem_flip_b = -1;
        }
    }

    for (int i = 0; i < total; i++) {
        int r = i / cols, c = i % cols;
        ImVec2 pp0(p0.x + 10 + c * (cw + 8), p0.y + 10 + r * (chh + 8));
        ImVec2 pp1(pp0.x + cw, pp0.y + cw);

        bool hover = mp.x >= pp0.x && mp.x <= pp1.x && mp.y >= pp0.y && mp.y <= pp1.y;
        bool show = g_mini.mem_flipped[i] || g_mini.mem_done[i];

        ImU32 bg = show ? IM_COL32(80 + g_mini.mem_cards[i] * 15, 120, 200 - g_mini.mem_cards[i] * 12, 255) : IM_COL32(60, 55, 80, 255);
        if (hover && !show) bg = IM_COL32(80, 75, 110, 255);

        dl->AddRectFilled(pp0, pp1, bg, 8.0f);
        dl->AddRect(pp0, pp1, ColF(t.accent, hover && !show ? 1.0f : 0.4f), 8.0f, 0, 1.5f);

        if (show) {
            char b[8]; snprintf(b, sizeof(b), "%d", g_mini.mem_cards[i]);
            ImVec2 ts = ImGui::CalcTextSize(b);
            dl->AddText(ImVec2(pp0.x + (cw - ts.x) * 0.5f, pp0.y + (cw - ts.y) * 0.5f), IM_COL32(255, 255, 255, 255), b);
        }

        if (hover && !show && g_mini.mem_flash_t <= 0.0f && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
            if (g_mini.mem_flip_a < 0) {
                g_mini.mem_flip_a = i;
                g_mini.mem_flipped[i] = true;
            }
            else if (g_mini.mem_flip_b < 0 && i != g_mini.mem_flip_a) {
                g_mini.mem_flip_b = i;
                g_mini.mem_flipped[i] = true;
                if (g_mini.mem_cards[g_mini.mem_flip_a] == g_mini.mem_cards[g_mini.mem_flip_b]) {
                    g_mini.mem_done[g_mini.mem_flip_a] = true;
                    g_mini.mem_done[g_mini.mem_flip_b] = true;
                    g_mini.score += 2;
                    g_mini.mem_flip_a = -1;
                    g_mini.mem_flip_b = -1;
                }
                else {
                    g_mini.mem_flash_t = 0.8f;
                }
            }
            // Проверка победы
            bool all = true;
            for (bool d : g_mini.mem_done) if (!d) { all = false; break; }
            if (all) MiniFinish();
        }
    }
}

static void DrawMiniGameClicker(ImVec2 p0, ImVec2 p1) {
    ThemeColors t = GetTheme();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    float dt = ImGui::GetIO().DeltaTime;

    if (!g_mini.running) {
        if (g_mini.finished) {
            ImVec2 c((p0.x + p1.x) * 0.5f, (p0.y + p1.y) * 0.5f);
            ImVec2 ts = ImGui::CalcTextSize(g_mini.message.c_str());
            dl->AddText(ImVec2(c.x - ts.x * 0.5f, c.y - ts.y * 0.5f), ColF(t.text, 1.0f), g_mini.message.c_str());
        }
        return;
    }

    g_mini.timer += dt;
    if (g_mini.timer >= g_mini.time_limit) { g_mini.score = g_mini.clicks; MiniFinish(); return; }

    ImVec2 c((p0.x + p1.x) * 0.5f, (p0.y + p1.y) * 0.5f);
    float pulse = 0.95f + 0.05f * std::sin((float)ImGui::GetTime() * 12.0f);
    float radius = 80.0f * pulse;

    dl->AddCircleFilled(c, radius + 8, IM_COL32(60, 40, 80, 200), 32);
    dl->AddCircleFilled(c, radius, ColF(t.accent, 0.9f), 32);
    dl->AddCircle(c, radius, ColF(t.text, 0.6f), 32, 2.0f);

    char b[32]; snprintf(b, sizeof(b), "%d", g_mini.clicks);
    ImVec2 ts = ImGui::CalcTextSize(b);
    dl->AddText(ImVec2(c.x - ts.x * 0.5f, c.y - ts.y * 0.5f), IM_COL32(255, 255, 255, 255), b);

    // Таймер
    float remain = g_mini.time_limit - g_mini.timer;
    char tb[32]; snprintf(tb, sizeof(tb), "%.1f сек", remain);
    ImVec2 tts = ImGui::CalcTextSize(tb);
    dl->AddText(ImVec2(c.x - tts.x * 0.5f, p0.y + 10), ColF(t.text, 1.0f), tb);

    ImVec2 mp = ImGui::GetIO().MousePos;
    float dx = mp.x - c.x, dy = mp.y - c.y;
    if (ImGui::IsMouseClicked(ImGuiMouseButton_Left) && dx * dx + dy * dy <= radius * radius) {
        g_mini.clicks++;
        g_mini.score = g_mini.clicks;
    }
}

static void DrawMiniGameMath(ImVec2 p0, ImVec2 p1) {
    ThemeColors t = GetTheme();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    float dt = ImGui::GetIO().DeltaTime;

    if (!g_mini.running) {
        if (g_mini.finished) {
            ImVec2 c((p0.x + p1.x) * 0.5f, (p0.y + p1.y) * 0.5f);
            ImVec2 ts = ImGui::CalcTextSize(g_mini.message.c_str());
            dl->AddText(ImVec2(c.x - ts.x * 0.5f, c.y - ts.y * 0.5f), ColF(t.text, 1.0f), g_mini.message.c_str());
        }
        return;
    }

    g_mini.timer += dt;
    if (g_mini.timer >= g_mini.time_limit) { MiniFinish(); return; }

    ImVec2 c((p0.x + p1.x) * 0.5f, (p0.y + p1.y) * 0.5f);
    char b[64]; snprintf(b, sizeof(b), "%d + %d = ?", g_mini.math_a, g_mini.math_b);
    ImVec2 ts = ImGui::CalcTextSize(b);
    dl->AddText(ImVec2(c.x - ts.x * 0.5f, c.y - 80), ColF(t.text, 1.0f), b);

    char sb[64]; snprintf(sb, sizeof(sb), "Решено: %d   ·   %.1f сек", g_mini.score, g_mini.time_limit - g_mini.timer);
    ImVec2 ss = ImGui::CalcTextSize(sb);
    dl->AddText(ImVec2(c.x - ss.x * 0.5f, p0.y + 10), ColF(t.text_disabled, 1.0f), sb);

    // Поле ввода — используем ImGui-виджет по фиксированной позиции
    ImGui::SetCursorScreenPos(ImVec2(c.x - 100, c.y));
    ImGui::PushID("math_input");
    ImGui::SetNextItemWidth(200);
    ImGui::PushStyleColor(ImGuiCol_FrameBg, IM_COL32(255, 255, 255, 15));
    if (ImGui::InputText("##mi", g_mini.math_input, IM_ARRAYSIZE(g_mini.math_input), ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_CharsDecimal)) {
        int val = atoi(g_mini.math_input);
        if (val == g_mini.math_answer) {
            g_mini.score++;
            g_mini.math_streak++;
            std::mt19937 rng((unsigned)std::chrono::system_clock::now().time_since_epoch().count());
            std::uniform_int_distribution<int> d(1, 20);
            g_mini.math_a = d(rng);
            g_mini.math_b = d(rng);
            g_mini.math_answer = g_mini.math_a + g_mini.math_b;
            g_mini.math_input[0] = 0;
        }
        else {
            g_mini.math_streak = 0;
            g_mini.math_input[0] = 0;
        }
    }
    ImGui::PopStyleColor();
    ImGui::PopID();

    ImGui::SetCursorScreenPos(ImVec2(p0.x, p1.y + 8));
    ImGui::Dummy(ImVec2(1, 1));
}

static void DrawMiniGameSnake(ImVec2 p0, ImVec2 p1) {
    ThemeColors t = GetTheme();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    float dt = ImGui::GetIO().DeltaTime;

    if (!g_mini.running) {
        if (g_mini.finished) {
            ImVec2 c((p0.x + p1.x) * 0.5f, (p0.y + p1.y) * 0.5f);
            ImVec2 ts = ImGui::CalcTextSize(g_mini.message.c_str());
            dl->AddText(ImVec2(c.x - ts.x * 0.5f, c.y - ts.y * 0.5f), ColF(t.text, 1.0f), g_mini.message.c_str());
        }
        return;
    }

    int grid = g_mini.snake_grid;
    float side = std::min(p1.x - p0.x, p1.y - p0.y) - 20.0f;
    float cell = side / grid;
    ImVec2 origin((p0.x + p1.x) * 0.5f - side * 0.5f, (p0.y + p1.y) * 0.5f - side * 0.5f);

    dl->AddRectFilled(origin, ImVec2(origin.x + side, origin.y + side), IM_COL32(20, 20, 30, 220), 6.0f);

    g_mini.snake_move_t += dt;
    float step = 0.14f;
    if (g_mini.snake_move_t >= step) {
        g_mini.snake_move_t -= step;

        // Управление
        ImGuiIO& io = ImGui::GetIO();
        if ((ImGui::IsKeyPressed(ImGuiKey_W) || ImGui::IsKeyPressed(ImGuiKey_UpArrow)) && g_mini.snake_dir.y == 0) g_mini.snake_dir = ImVec2(0, -1);
        else if ((ImGui::IsKeyPressed(ImGuiKey_S) || ImGui::IsKeyPressed(ImGuiKey_DownArrow)) && g_mini.snake_dir.y == 0) g_mini.snake_dir = ImVec2(0, 1);
        else if ((ImGui::IsKeyPressed(ImGuiKey_A) || ImGui::IsKeyPressed(ImGuiKey_LeftArrow)) && g_mini.snake_dir.x == 0) g_mini.snake_dir = ImVec2(-1, 0);
        else if ((ImGui::IsKeyPressed(ImGuiKey_D) || ImGui::IsKeyPressed(ImGuiKey_RightArrow)) && g_mini.snake_dir.x == 0) g_mini.snake_dir = ImVec2(1, 0);

        ImVec2 head = g_mini.snake[0];
        ImVec2 nh(head.x + g_mini.snake_dir.x, head.y + g_mini.snake_dir.y);
        if (nh.x < 0 || nh.x >= grid || nh.y < 0 || nh.y >= grid) { MiniFinish(); return; }

        // Проверка на столкновение с телом
        for (auto& seg : g_mini.snake) {
            if (seg.x == nh.x && seg.y == nh.y) { MiniFinish(); return; }
        }

        g_mini.snake.insert(g_mini.snake.begin(), nh);

        // Яблоко
        bool ate = false;
        for (size_t i = 0; i < g_mini.apples.size(); i++) {
            if (g_mini.apples[i].x == nh.x && g_mini.apples[i].y == nh.y) {
                g_mini.apples.erase(g_mini.apples.begin() + i);
                g_mini.score++;
                ate = true;
                break;
            }
        }
        if (!ate) g_mini.snake.pop_back();

        if (g_mini.apples.empty()) {
            std::mt19937 rng((unsigned)std::chrono::system_clock::now().time_since_epoch().count());
            std::uniform_int_distribution<int> d(0, grid - 1);
            g_mini.apples.push_back(ImVec2((float)d(rng), (float)d(rng)));
        }
    }

    for (auto& seg : g_mini.snake) {
        ImVec2 s0(origin.x + seg.x * cell + 1, origin.y + seg.y * cell + 1);
        ImVec2 s1(s0.x + cell - 2, s0.y + cell - 2);
        dl->AddRectFilled(s0, s1, ColF(t.accent, 0.9f), 3.0f);
    }
    for (auto& ap : g_mini.apples) {
        ImVec2 c(origin.x + ap.x * cell + cell * 0.5f, origin.y + ap.y * cell + cell * 0.5f);
        dl->AddCircleFilled(c, cell * 0.4f, IM_COL32(220, 60, 60, 255), 12);
    }

    char b[64]; snprintf(b, sizeof(b), "Яблок: %d", g_mini.score);
    dl->AddText(ImVec2(origin.x, origin.y - 22), ColF(t.text, 1.0f), b);
}

static void DrawMiniGameColor(ImVec2 p0, ImVec2 p1) {
    ThemeColors t = GetTheme();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    float dt = ImGui::GetIO().DeltaTime;

    if (!g_mini.running) {
        if (g_mini.finished) {
            ImVec2 c((p0.x + p1.x) * 0.5f, (p0.y + p1.y) * 0.5f);
            ImVec2 ts = ImGui::CalcTextSize(g_mini.message.c_str());
            dl->AddText(ImVec2(c.x - ts.x * 0.5f, c.y - ts.y * 0.5f), ColF(t.text, 1.0f), g_mini.message.c_str());
        }
        return;
    }

    g_mini.timer += dt;
    if (g_mini.timer >= g_mini.time_limit) { MiniFinish(); return; }

    ImVec2 c((p0.x + p1.x) * 0.5f, (p0.y + p1.y) * 0.5f);

    // Целевой цвет
    ImVec2 t0(c.x - 250, c.y - 60);
    ImVec2 t1(c.x - 30, c.y + 60);
    dl->AddRectFilled(t0, t1, IM_COL32((int)(g_mini.col_target_r * 255), (int)(g_mini.col_target_g * 255), (int)(g_mini.col_target_b * 255), 255), 8.0f);
    dl->AddRect(t0, t1, ColF(t.text, 0.5f), 8.0f, 0, 2.0f);

    // Предположение
    ImVec2 g0(c.x + 30, c.y - 60);
    ImVec2 g1(c.x + 250, c.y + 60);
    dl->AddRectFilled(g0, g1, IM_COL32((int)(g_mini.col_guess_r * 255), (int)(g_mini.col_guess_g * 255), (int)(g_mini.col_guess_b * 255), 255), 8.0f);
    dl->AddRect(g0, g1, ColF(t.text, 0.5f), 8.0f, 0, 2.0f);

    // Слайдеры RGB
    ImGui::SetCursorScreenPos(ImVec2(c.x - 250, c.y + 90));
    ImGui::PushID("color_sliders");
    ImGui::PushItemWidth(500);
    ImGui::SliderFloat("##r", &g_mini.col_guess_r, 0.0f, 1.0f, "R %.2f");
    ImGui::SliderFloat("##g", &g_mini.col_guess_g, 0.0f, 1.0f, "G %.2f");
    ImGui::SliderFloat("##b", &g_mini.col_guess_b, 0.0f, 1.0f, "B %.2f");
    ImGui::PopItemWidth();

    if (ImGui::Button("Проверить", ImVec2(500, 30))) {
        float dr = g_mini.col_target_r - g_mini.col_guess_r;
        float dg = g_mini.col_target_g - g_mini.col_guess_g;
        float db = g_mini.col_target_b - g_mini.col_guess_b;
        float dist = std::sqrt(dr * dr + dg * dg + db * db);
        if (dist < 0.15f) {
            g_mini.score++;
            std::mt19937 rng((unsigned)std::chrono::system_clock::now().time_since_epoch().count());
            std::uniform_real_distribution<float> d(0.1f, 0.9f);
            g_mini.col_target_r = d(rng); g_mini.col_target_g = d(rng); g_mini.col_target_b = d(rng);
        }
        else {
            g_mini.col_streak = 0;
        }
    }
    ImGui::PopID();

    char b[64]; snprintf(b, sizeof(b), "Угадано: %d   ·   %.1f", g_mini.score, g_mini.time_limit - g_mini.timer);
    dl->AddText(ImVec2(c.x - 250, c.y - 100), ColF(t.text, 1.0f), b);

    ImGui::SetCursorScreenPos(ImVec2(p0.x, p1.y + 8));
    ImGui::Dummy(ImVec2(1, 1));
}

static void DrawMiniGameTyping(ImVec2 p0, ImVec2 p1) {
    ThemeColors t = GetTheme();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    float dt = ImGui::GetIO().DeltaTime;

    if (!g_mini.running) {
        if (g_mini.finished) {
            ImVec2 c((p0.x + p1.x) * 0.5f, (p0.y + p1.y) * 0.5f);
            ImVec2 ts = ImGui::CalcTextSize(g_mini.message.c_str());
            dl->AddText(ImVec2(c.x - ts.x * 0.5f, c.y - ts.y * 0.5f), ColF(t.text, 1.0f), g_mini.message.c_str());
        }
        return;
    }

    g_mini.timer += dt;
    if (g_mini.timer >= g_mini.time_limit) { MiniFinish(); return; }
    if (g_mini.type_start == 0.0f) g_mini.type_start = g_mini.timer;

    ImVec2 c((p0.x + p1.x) * 0.5f, (p0.y + p1.y) * 0.5f);

    // Целевой текст с подсветкой
    float x = c.x - 300, y = c.y - 40;
    for (size_t i = 0; i < g_mini.type_target.size(); i++) {
        char ch[2] = { g_mini.type_target[i], 0 };
        ImVec2 sz = ImGui::CalcTextSize(ch);
        ImU32 col = ColF(t.text_disabled, 1.0f);
        if (i < g_mini.type_text.size()) {
            col = (g_mini.type_text[i] == g_mini.type_target[i]) ? IM_COL32(80, 220, 120, 255) : IM_COL32(220, 80, 80, 255);
        }
        else if (i == g_mini.type_text.size()) {
            col = ColF(t.accent, 1.0f);
        }
        dl->AddText(ImVec2(x, y), col, ch);
        x += sz.x;
    }

    // Поле ввода
    ImGui::SetCursorScreenPos(ImVec2(c.x - 300, c.y + 30));
    ImGui::PushID("type_input");
    ImGui::SetNextItemWidth(600);
    ImGui::PushStyleColor(ImGuiCol_FrameBg, IM_COL32(255, 255, 255, 15));
    char buf[256];
    strncpy_s(buf, sizeof(buf), g_mini.type_text.c_str(), _TRUNCATE);
    if (ImGui::InputText("##ti", buf, IM_ARRAYSIZE(buf))) {
        g_mini.type_text = buf;
    }
    ImGui::PopStyleColor();
    ImGui::PopID();

    // Проверка завершения
    if (g_mini.type_text == g_mini.type_target) {
        float secs = g_mini.timer - g_mini.type_start;
        float wpm = (float)g_mini.type_target.size() / 5.0f / (secs / 60.0f);
        g_mini.score = (int)wpm;
        MiniFinish();
    }

    float secs = g_mini.timer - g_mini.type_start;
    char b[64]; snprintf(b, sizeof(b), "Время: %.1f сек", secs);
    dl->AddText(ImVec2(c.x - 300, c.y - 80), ColF(t.text, 1.0f), b);

    ImGui::SetCursorScreenPos(ImVec2(p0.x, p1.y + 8));
    ImGui::Dummy(ImVec2(1, 1));
}

static void DrawMiniGameAim(ImVec2 p0, ImVec2 p1) {
    ThemeColors t = GetTheme();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    float dt = ImGui::GetIO().DeltaTime;
    ImVec2 mp = ImGui::GetIO().MousePos;

    if (!g_mini.running) {
        if (g_mini.finished) {
            ImVec2 c((p0.x + p1.x) * 0.5f, (p0.y + p1.y) * 0.5f);
            ImVec2 ts = ImGui::CalcTextSize(g_mini.message.c_str());
            dl->AddText(ImVec2(c.x - ts.x * 0.5f, c.y - ts.y * 0.5f), ColF(t.text, 1.0f), g_mini.message.c_str());
        }
        return;
    }

    g_mini.timer += dt;
    if (g_mini.timer >= g_mini.time_limit) { MiniFinish(); return; }

    g_mini.aim_spawn_t += dt;
    if (g_mini.aim_spawn_t > 0.7f && g_mini.aim_targets.size() < 3) {
        g_mini.aim_spawn_t = 0.0f;
        std::mt19937 rng((unsigned)std::chrono::system_clock::now().time_since_epoch().count());
        std::uniform_real_distribution<float> dx(p0.x + 60, p1.x - 60);
        std::uniform_real_distribution<float> dy(p0.y + 60, p1.y - 60);
        g_mini.aim_targets.push_back(ImVec2(dx(rng), dy(rng)));
    }

    for (size_t i = 0; i < g_mini.aim_targets.size(); ) {
        ImVec2 c = g_mini.aim_targets[i];
        float dx = mp.x - c.x, dy = mp.y - c.y;
        bool hit = dx * dx + dy * dy <= 30 * 30;
        dl->AddCircleFilled(c, 30, ColF(t.accent, hit ? 0.9f : 0.6f), 24);
        dl->AddCircle(c, 30, ColF(t.text, 0.7f), 24, 2.0f);
        dl->AddCircleFilled(c, 6, IM_COL32(255, 255, 255, 200), 12);

        if (hit && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
            g_mini.score++;
            g_mini.aim_targets.erase(g_mini.aim_targets.begin() + i);
        }
        else i++;
    }

    char b[64]; snprintf(b, sizeof(b), "Попадания: %d   ·   %.1f", g_mini.score, g_mini.time_limit - g_mini.timer);
    dl->AddText(ImVec2(p0.x + 20, p0.y + 20), ColF(t.text, 1.0f), b);
}

static void DrawMiniGamesPage() {
    ThemeColors t = GetTheme();
    ImDrawList* dl = ImGui::GetWindowDrawList();

    // Обновляем таймер активной игры
    if (g_mini.running) {
        // Обновляется внутри DrawMiniGame*
    }

    ImGui::Dummy(ImVec2(0, 4));
    ImGui::SetCursorPosX(ImGui::GetCursorPosX() + 24.0f);

    // Уровень сверху
    {
        int lvl = LevelFromXP(g_xp);
        int cur = XPIntoLevel(g_xp);
        int need = XPRemaining(g_xp);

        ImVec2 wp = ImGui::GetCursorScreenPos();
        float width = ImGui::GetContentRegionAvail().x - 48.0f;

        // Рамка
        dl->AddRectFilled(ImVec2(wp.x, wp.y), ImVec2(wp.x + width, wp.y + 80), ColF(t.accent, 0.10f), 12.0f);
        dl->AddRect(ImVec2(wp.x, wp.y), ImVec2(wp.x + width, wp.y + 80), ColF(t.accent, 0.55f), 12.0f, 0, 1.5f);

        char lvl_buf[64]; snprintf(lvl_buf, sizeof(lvl_buf), "Уровень %d · %s", lvl, LevelTitle(lvl));
        dl->AddText(ImVec2(wp.x + 20, wp.y + 12), ColF(t.text, 1.0f), lvl_buf);

        char xp_buf[64]; snprintf(xp_buf, sizeof(xp_buf), "XP: %d / %d", cur, need);
        ImVec2 xp_sz = ImGui::CalcTextSize(xp_buf);
        dl->AddText(ImVec2(wp.x + width - xp_sz.x - 20, wp.y + 14), ColF(t.text_disabled, 1.0f), xp_buf);

        // Прогресс-бар
        ImVec2 b0(wp.x + 20, wp.y + 45);
        ImVec2 b1(wp.x + width - 20, wp.y + 65);
        dl->AddRectFilled(b0, b1, IM_COL32(255, 255, 255, 15), 10.0f);
        float frac = (need > 0) ? (float)cur / need : 0.0f;
        if (frac > 1.0f) frac = 1.0f;
        dl->AddRectFilled(b0, ImVec2(b0.x + (b1.x - b0.x) * frac, b1.y), ColF(t.accent, 1.0f), 10.0f);

        ImGui::Dummy(ImVec2(width, 86));
    }

    // Заголовок
    ImGui::TextDisabled("Мини-игры");
    ImGui::Spacing();

    // Если игра активна — рисуем её
    if (g_mini.current != MiniGame::None) {
        ImVec2 avail = ImGui::GetContentRegionAvail();
        float top_h = 60.0f;
        ImVec2 game_p0 = ImGui::GetCursorScreenPos();
        ImVec2 game_p1 = ImVec2(game_p0.x + avail.x - 48, game_p0.y + avail.y - top_h);

        // Рамка игровой зоны
        dl->AddRectFilled(game_p0, game_p1, ColF(t.window_bg, 0.35f), 12.0f);
        dl->AddRect(game_p0, game_p1, ColF(t.accent, 0.55f), 12.0f, 0, 1.5f);

        // Кнопка "Назад"
        ImGui::SetCursorScreenPos(ImVec2(game_p0.x + 12, game_p0.y + 12));
        ImGui::PushStyleColor(ImGuiCol_Button, ColF(t.accent, 0.20f));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ColF(t.accent, 0.35f));
        if (ImGui::Button("← К списку", ImVec2(120, 30))) {
            g_mini = MiniGameState{};
        }
        ImGui::PopStyleColor(2);

        // Название
        dl->AddText(ImVec2(game_p0.x + 150, game_p0.y + 18), ColF(t.text, 1.0f), MiniName(g_mini.current));

        // Игровая зона
        switch (g_mini.current) {
        case MiniGame::Reaction: DrawMiniGameReaction(game_p0, game_p1); break;
        case MiniGame::Memory:   DrawMiniGameMemory(game_p0, game_p1); break;
        case MiniGame::Clicker:  DrawMiniGameClicker(game_p0, game_p1); break;
        case MiniGame::Math:     DrawMiniGameMath(game_p0, game_p1); break;
        case MiniGame::Snake:    DrawMiniGameSnake(game_p0, game_p1); break;
        case MiniGame::Color:    DrawMiniGameColor(game_p0, game_p1); break;
        case MiniGame::Typing:   DrawMiniGameTyping(game_p0, game_p1); break;
        case MiniGame::Aim:      DrawMiniGameAim(game_p0, game_p1); break;
        default: break;
        }

        ImGui::SetCursorScreenPos(ImVec2(game_p0.x, game_p1.y + 4));
        ImGui::Dummy(ImVec2(1, 1));
    }
    else {
        // Сетка игр
        ImVec2 avail = ImGui::GetContentRegionAvail();
        int cols = 4;
        float gap = 14.0f;
        float cw = (avail.x - 48 - gap * (cols - 1)) / cols;
        float ch = 180.0f;
        ImVec2 origin = ImGui::GetCursorScreenPos();
        origin.x += 0;

        ImVec2 mp = ImGui::GetIO().MousePos;
        int idx = 0;
        for (int g = 1; g < (int)MiniGame::Count; g++) {
            int col = idx % cols, row = idx / cols;
            ImVec2 p0(origin.x + col * (cw + gap), origin.y + row * (ch + gap));
            ImVec2 p1(p0.x + cw, p0.y + ch);
            bool hv = mp.x >= p0.x && mp.x <= p1.x && mp.y >= p0.y && mp.y <= p1.y;

            ImU32 bg = hv ? ColF(t.accent, 0.18f) : ColF(t.window_bg, 0.35f);
            dl->AddRectFilled(p0, p1, bg, 12.0f);
            dl->AddRect(p0, p1, ColF(t.accent, hv ? 1.0f : 0.45f), 12.0f, 0, 1.5f);

            dl->AddText(ImVec2(p0.x + 16, p0.y + 14), ColF(t.text, 1.0f), MiniName((MiniGame)g));
            dl->AddText(ImVec2(p0.x + 16, p0.y + 40), ColF(t.text_disabled, 1.0f), MiniDesc((MiniGame)g));

            // Звёзды
            DrawStarsInline(dl, ImVec2(p0.x + 20, p0.y + ch - 70), g_mini_scores[g].stars, 18.0f);

            // Лучший результат
            char b[64];
            if (g == (int)MiniGame::Reaction)
                snprintf(b, sizeof(b), "Лучший: %d мс", g_mini_scores[g].best);
            else
                snprintf(b, sizeof(b), "Лучший: %d", g_mini_scores[g].best);
            dl->AddText(ImVec2(p0.x + 16, p0.y + ch - 36), ColF(t.text_disabled, 1.0f), b);

            if (hv && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
                MiniStart((MiniGame)g);
            }
            idx++;
        }
        ImGui::Dummy(ImVec2(avail.x, ((idx + cols - 1) / cols) * (ch + gap)));
    }
}

// ============================================================
//  БИБЛИОТЕКА
// ============================================================
static void DrawLibraryPage() {
    ThemeColors t = GetTheme();
    bool busy = g_app.IsBusy();
    if (busy) ImGui::BeginDisabled();

    ImGui::Dummy(ImVec2(0, 4));
    ImGui::Indent(32.0f);
    if (ImGui::Button("Обновить список", ImVec2(180, 34))) { RefreshInstalledFiles(); RefreshInstalledFlags(); LogPush("Список библиотеки обновлён"); }
    ImGui::SameLine(0, 12);
    if (ImGui::Button("Открыть папку Lua", ImVec2(180, 34))) OpenLuaFolder();
    ImGui::Unindent(24.0f);
    ImGui::Spacing(); ImGui::Separator(); ImGui::Spacing();

    if (g_app.installed.empty()) {
        ImGui::TextDisabled("В библиотеке пока нет установленных игр.");
        if (busy) ImGui::EndDisabled();
        return;
    }

    ImGui::Indent(24.0f);
    ImGui::BeginChild("##lib_scroll", ImVec2(0, 0), false);
    std::string to_delete;
    for (size_t i = 0; i < g_app.installed.size(); ++i) {
        auto& e = g_app.installed[i];
        ImGui::PushID(e.appid.c_str());
        ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(1, 1, 1, 0.035f));
        ImGui::BeginChild("##row", ImVec2(0, 68), true, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
        ImGui::SetCursorPos(ImVec2(18, 10));
        if (g_font_big) ImGui::PushFont(g_font_big);
        ImGui::Text("%s", e.name.c_str());
        if (g_font_big) ImGui::PopFont();
        ImGui::SetCursorPos(ImVec2(18, 40));
        ImGui::TextDisabled("AppID: %s", e.appid.c_str());
        ImGui::SetCursorPos(ImVec2(ImGui::GetWindowWidth() - 340, 16));
        ImGui::PushStyleColor(ImGuiCol_Button, t.accent);
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, t.accent_hover);
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, t.accent_active);
        if (ImGui::Button("Скачать заново", ImVec2(160, 36))) DownloadManifestAsync(e.appid, e.name);
        ImGui::PopStyleColor(3);
        ImGui::SetCursorPos(ImVec2(ImGui::GetWindowWidth() - 170, 16));
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.80f, 0.25f, 0.35f, 0.9f));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.95f, 0.35f, 0.45f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.65f, 0.18f, 0.28f, 1.0f));
        if (ImGui::Button("Удалить", ImVec2(150, 36))) to_delete = e.appid;
        ImGui::PopStyleColor(3);
        ImGui::EndChild();
        ImGui::PopStyleColor();
        ImGui::PopID();
        ImGui::Dummy(ImVec2(0, 6));
    }
    ImGui::EndChild();
    ImGui::Unindent(24.0f);
    if (!to_delete.empty()) DeleteInstalled(to_delete);
    if (busy) ImGui::EndDisabled();
}

// ============================================================
//  ИНСТРУМЕНТЫ
// ============================================================
static void DrawFxButton(const char* label, FxMode mode) {
    ThemeColors t = GetTheme();
    bool active = (g_fx == mode);
    if (active) {
        ImGui::PushStyleColor(ImGuiCol_Button, t.accent);
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, t.accent_hover);
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, t.accent_active);
    }
    if (ImGui::Button(label, ImVec2(140, 34))) {
        g_fx = mode;
        g_stat_fx++; SaveSettings();
        UnlockAchievement("fx_change");
        if (g_stat_fx >= 20) UnlockAchievement("fx_all");
    }
    if (active) ImGui::PopStyleColor(3);
}
static void DrawThemeButton(const char* label, ThemeMode mode) {
    ThemeColors tc = GetTheme();
    bool active = (g_theme == mode);
    if (active) {
        ImGui::PushStyleColor(ImGuiCol_Button, tc.accent);
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, tc.accent_hover);
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, tc.accent_active);
    }
    if (ImGui::Button(label, ImVec2(120, 34))) {
        g_theme = mode; ApplyStyle();
        g_stat_theme++; SaveSettings();
        UnlockAchievement("theme_change");
        if (g_stat_theme >= 20) UnlockAchievement("theme_all");
    }
    if (active) ImGui::PopStyleColor(3);
}

static bool g_custom_popup_request = false;

static void DrawCustomGradientPopup() {
    ThemeColors t = GetTheme();
    if (g_custom_popup_request) {
        ImGui::OpenPopup("##custom_gradient_modal");
        g_custom_popup_request = false;
    }

    ImVec2 center = ImGui::GetMainViewport()->GetCenter();
    ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSize(ImVec2(700, 480), ImGuiCond_Always);

    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 14.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(24, 20));
    ImGui::PushStyleColor(ImGuiCol_PopupBg, ImVec4(t.window_bg.x + 0.02f, t.window_bg.y + 0.02f, t.window_bg.z + 0.04f, 0.99f));
    ImGui::PushStyleColor(ImGuiCol_Border, ColF(t.accent, 0.6f));

    bool open = true;
    ImGuiWindowFlags flags =
        ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse |
        ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoTitleBar |
        ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse;

    if (ImGui::BeginPopupModal("##custom_gradient_modal", &open, flags)) {
        ImGui::TextColored(t.accent, "Свой градиент");
        ImGui::Spacing(); ImGui::Separator(); ImGui::Spacing();

        const float picker_w = 280.0f;
        ImGui::BeginGroup();
        ImGui::TextDisabled("Цвет A");
        ImGui::PushItemWidth(picker_w);
        ImGui::ColorPicker4("##ca", g_custom_color_a,
            ImGuiColorEditFlags_NoSidePreview | ImGuiColorEditFlags_NoSmallPreview |
            ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_AlphaBar);
        ImGui::PopItemWidth();
        ImGui::EndGroup();
        ImGui::SameLine(0, 24);
        ImGui::BeginGroup();
        ImGui::TextDisabled("Цвет B");
        ImGui::PushItemWidth(picker_w);
        ImGui::ColorPicker4("##cb", g_custom_color_b,
            ImGuiColorEditFlags_NoSidePreview | ImGuiColorEditFlags_NoSmallPreview |
            ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_AlphaBar);
        ImGui::PopItemWidth();
        ImGui::EndGroup();

        ImGui::Spacing(); ImGui::Spacing();

        {
            ImVec2 pv0 = ImGui::GetCursorScreenPos();
            float pv_w = ImGui::GetContentRegionAvail().x;
            ImVec2 pv1 = ImVec2(pv0.x + pv_w, pv0.y + 44.0f);
            ImDrawList* dl = ImGui::GetWindowDrawList();
            dl->AddRectFilledMultiColor(pv0, pv1,
                IM_COL32((int)(g_custom_color_a[0] * 255), (int)(g_custom_color_a[1] * 255), (int)(g_custom_color_a[2] * 255), 255),
                IM_COL32((int)(g_custom_color_b[0] * 255), (int)(g_custom_color_b[1] * 255), (int)(g_custom_color_b[2] * 255), 255),
                IM_COL32((int)(g_custom_color_b[0] * 255), (int)(g_custom_color_b[1] * 255), (int)(g_custom_color_b[2] * 255), 255),
                IM_COL32((int)(g_custom_color_a[0] * 255), (int)(g_custom_color_a[1] * 255), (int)(g_custom_color_a[2] * 255), 255));
            dl->AddRect(pv0, pv1, ColF(t.accent, 0.5f), 10.0f, 0, 1.5f);
            ImGui::Dummy(ImVec2(pv_w, 52.0f));
        }

        ImGui::Spacing();

        float btn_w = 160.0f, btn_h = 38.0f;
        float total_w = btn_w * 2 + 12.0f;
        float avail_w = ImGui::GetContentRegionAvail().x;
        ImGui::SetCursorPosX(ImGui::GetCursorPosX() + (avail_w - total_w) * 0.5f);

        ImGui::PushStyleColor(ImGuiCol_Button, t.accent);
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, t.accent_hover);
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, t.accent_active);
        if (ImGui::Button("Применить", ImVec2(btn_w, btn_h))) {
            g_theme = ThemeMode::Custom;
            ApplyStyle();
            UnlockAchievement("gradient");
            SaveSettings();
            PushToast("Свой градиент применён");
            ImGui::CloseCurrentPopup();
        }
        ImGui::PopStyleColor(3);
        ImGui::SameLine(0, 12);
        if (ImGui::Button("Отмена", ImVec2(btn_w, btn_h))) {
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }
    ImGui::PopStyleColor(2);
    ImGui::PopStyleVar(2);
}

static void DrawToolsPage() {
    ThemeColors t = GetTheme();
    bool busy = g_app.IsBusy();
    if (busy) ImGui::BeginDisabled();

    ImGui::Dummy(ImVec2(0, 4));
    ImGui::Indent(24.0f);
    ImGui::TextDisabled("Инструменты установки и управления Steam");
    ImGui::Spacing(); ImGui::Separator(); ImGui::Spacing();

    ImGui::PushStyleColor(ImGuiCol_Button, t.accent);
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, t.accent_hover);
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, t.accent_active);
    if (ImGui::Button("Установить OpenSteamTools", ImVec2(280, 40))) InstallOpenSteamToolsAsync();
    ImGui::PopStyleColor(3);
    ImGui::SameLine(0, 12);
    if (ImGui::Button("Установить ZIP вручную", ImVec2(280, 40))) {
        OPENFILENAMEA ofn = {};
        char file[MAX_PATH] = {};
        ofn.lStructSize = sizeof(ofn);
        ofn.lpstrFilter = "ZIP-архивы\0*.zip\0Все файлы\0*.*\0";
        ofn.lpstrFile = file; ofn.nMaxFile = MAX_PATH;
        ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST;
        if (GetOpenFileNameA(&ofn)) InstallZipFile(file);
    }
    ImGui::Spacing();
    if (ImGui::Button("Закрыть Steam", ImVec2(280, 40))) CloseSteam();
    ImGui::SameLine(0, 12);
    if (ImGui::Button("Перезапустить Steam", ImVec2(280, 40))) RestartSteam();
    ImGui::Spacing();
    if (ImGui::Button("Открыть папку config/lua", ImVec2(280, 40))) OpenLuaFolder();

    ImGui::Spacing(); ImGui::Separator(); ImGui::Spacing();
    ImGui::TextDisabled("Цветовая тема");
    DrawThemeButton("Синяя", ThemeMode::Blue); ImGui::SameLine();
    DrawThemeButton("Фиолет", ThemeMode::Purple); ImGui::SameLine();
    DrawThemeButton("Зелёная", ThemeMode::Green); ImGui::SameLine();
    DrawThemeButton("Красная", ThemeMode::Red); ImGui::SameLine();
    DrawThemeButton("Оранж", ThemeMode::Orange);
    DrawThemeButton("Розовая", ThemeMode::Pink); ImGui::SameLine();
    DrawThemeButton("Бирюз", ThemeMode::Teal); ImGui::SameLine();
    DrawThemeButton("Жёлтая", ThemeMode::Yellow); ImGui::SameLine();
    DrawThemeButton("Серая", ThemeMode::Gray); ImGui::SameLine();
    DrawThemeButton("Закат", ThemeMode::Sunset);
    DrawThemeButton("Океан", ThemeMode::Ocean); ImGui::SameLine();
    DrawThemeButton("Лес", ThemeMode::Forest); ImGui::SameLine();

    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(t.accent2.x, t.accent2.y, t.accent2.z, 0.30f));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(t.accent2.x, t.accent2.y, t.accent2.z, 0.50f));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(t.accent2.x, t.accent2.y, t.accent2.z, 0.70f));
    if (ImGui::Button("+", ImVec2(120, 34))) g_custom_popup_request = true;
    ImGui::PopStyleColor(3);

    ImGui::Spacing(); ImGui::Separator(); ImGui::Spacing();
    ImGui::TextDisabled("Фоновая анимация");
    DrawFxButton("Аврора", FxMode::Aurora); ImGui::SameLine();
    DrawFxButton("Волны", FxMode::Waves); ImGui::SameLine();
    DrawFxButton("Частицы", FxMode::Particles); ImGui::SameLine();
    DrawFxButton("Градиент", FxMode::Gradient); ImGui::SameLine();
    DrawFxButton("Сетка", FxMode::Grid);

    ImGui::Spacing();
    ImGui::TextDisabled("Погодные эффекты");
    bool s1 = g_fx_snow, s2 = g_fx_rain, s3 = g_fx_leaves;
    if (ImGui::Checkbox("Снег", &s1)) { g_fx_snow = s1; g_stat_weather++; if (s1) UnlockAchievement("snow_on"); SaveSettings(); }
    ImGui::SameLine();
    if (ImGui::Checkbox("Дождь", &s2)) { g_fx_rain = s2; g_stat_weather++; if (s2) UnlockAchievement("rain_on"); SaveSettings(); }
    ImGui::SameLine();
    if (ImGui::Checkbox("Листья", &s3)) { g_fx_leaves = s3; g_stat_weather++; if (s3) UnlockAchievement("leaves_on"); SaveSettings(); }
    if (g_fx_snow && g_fx_rain && g_fx_leaves) UnlockAchievement("weather_all");

    ImGui::Spacing();
    ImGui::TextDisabled("Прочее");

    ImGui::Unindent(24.0f);
    if (busy) ImGui::EndDisabled();
}

// ============================================================
//  OVERLAY ПРОГРЕССА
// ============================================================
static void DrawOverlayAndProgress() {
    ProgressMode mode = (ProgressMode)g_app.progress_mode.load();
    ThemeColors t = GetTheme();
    float dt = ImGui::GetIO().DeltaTime; if (dt <= 0.0f) dt = 0.016f;
    float target = g_app.progress_target.load();
    if (target <= 0.0f) g_app.progress_display = 0.0f;
    else g_app.progress_display += (target - g_app.progress_display) * 0.9f * dt;

    bool want = (mode != ProgressMode::None) || g_app.success_visible;
    float ovt = want ? 0.5f : 0.0f;
    g_app.overlay_alpha += (ovt - g_app.overlay_alpha) * 3.0f * dt;
    if (g_app.overlay_alpha < 0.001f) g_app.overlay_alpha = 0.0f;

    ImGuiIO& io = ImGui::GetIO();
    ImDrawList* fg = ImGui::GetForegroundDrawList();
    if (g_app.overlay_alpha > 0.001f)
        fg->AddRectFilled(ImVec2(0, 0), io.DisplaySize, IM_COL32(0, 0, 0, (int)(g_app.overlay_alpha * 255)));

    if (mode != ProgressMode::None) {
        const float ww = 560.0f, wh = 200.0f;
        float wx = (io.DisplaySize.x - ww) * 0.5f, wy = (io.DisplaySize.y - wh) * 0.5f;
        fg->AddRectFilled(ImVec2(wx, wy), ImVec2(wx + ww, wy + wh), IM_COL32(20, 16, 32, 255), 14.0f);
        fg->AddRect(ImVec2(wx, wy), ImVec2(wx + ww, wy + wh), ColF(t.accent, 0.6f), 14.0f, 0, 1.5f);
        const char* title = "Загрузка манифеста";
        const char* sub = "удалит скачанное этой игры";
        ImU32 bar = ColF(t.accent, 1.0f);
        if (mode == ProgressMode::Delete) { title = "Удаление файлов"; sub = "идёт удаление..."; bar = IM_COL32(217, 89, 110, 255); }
        else if (mode == ProgressMode::InstallTools) { title = "Установка OpenSteamTools"; sub = "распаковка..."; bar = IM_COL32(64, 179, 115, 255); }
        fg->AddText(ImVec2(wx + 24, wy + 20), ColF(t.accent, 1.0f), title);
        { std::lock_guard<std::mutex> lk(g_app.status_mtx); fg->AddText(ImVec2(wx + 24, wy + 52), IM_COL32(230, 230, 240, 255), g_app.current_name.c_str()); }
        const float bx = wx + 24, by = wy + 92, bw = ww - 48, bh = 24;
        fg->AddRectFilled(ImVec2(bx, by), ImVec2(bx + bw, by + bh), IM_COL32(255, 255, 255, 20), 12.0f);
        float prog = MY_CLAMP(g_app.progress_display, 0.0f, 1.0f);
        fg->AddRectFilled(ImVec2(bx, by), ImVec2(bx + bw * prog, by + bh), bar, 12.0f);
        char b[48];
        if (mode == ProgressMode::Delete) snprintf(b, sizeof(b), "Удаление... %.0f%%", prog * 100.0f);
        else snprintf(b, sizeof(b), "%.0f%%", prog * 100.0f);
        ImVec2 tsz = ImGui::CalcTextSize(b);
        fg->AddText(ImVec2(bx + (bw - tsz.x) * 0.5f, by + (bh - tsz.y) * 0.5f), IM_COL32(255, 255, 255, 255), b);
        fg->AddText(ImVec2(wx + 24, wy + 140), IM_COL32(160, 160, 180, 255), sub);
        if (mode == ProgressMode::Download) {
            float bbx = wx + 24, bby = wy + 160, bbw = 120, bbh = 30;
            ImVec2 mp = io.MousePos;
            bool hv = mp.x >= bbx && mp.x <= bbx + bbw && mp.y >= bby && mp.y <= bby + bbh;
            ImU32 bc = hv ? IM_COL32(230, 90, 90, 255) : IM_COL32(190, 65, 65, 255);
            fg->AddRectFilled(ImVec2(bbx, bby), ImVec2(bbx + bbw, bby + bbh), bc, 8.0f);
            ImVec2 cts = ImGui::CalcTextSize("Отмена");
            fg->AddText(ImVec2(bbx + (bbw - cts.x) * 0.5f, bby + (bbh - cts.y) * 0.5f), IM_COL32(255, 255, 255, 255), "Отмена");
            if (hv && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) CancelCurrentDownload();
        }
    }

    if (g_app.success_visible) {
        g_app.success_timer += dt;
        const float t_in = 0.15f, t_hold = 0.35f, t_out = 0.20f;
        float total = t_in + t_hold + t_out;
        if (g_app.success_timer < t_in) g_app.success_alpha = g_app.success_timer / t_in;
        else if (g_app.success_timer < t_in + t_hold) g_app.success_alpha = 1.0f;
        else if (g_app.success_timer < total) g_app.success_alpha = 1.0f - (g_app.success_timer - t_in - t_hold) / t_out;
        else { g_app.success_alpha = 0.0f; g_app.success_visible = false; g_app.success_timer = 0.0f; }
        if (g_app.success_visible && g_app.success_alpha > 0.001f) {
            float cx = io.DisplaySize.x * 0.5f, cy = io.DisplaySize.y * 0.5f, a = g_app.success_alpha;
            fg->AddCircleFilled(ImVec2(cx, cy), 90.0f, ColF(t.accent, 0.7f * a));
            fg->AddCircleFilled(ImVec2(cx, cy), 72.0f, ColF(t.accent, a));
            const float th = 10.0f;
            ImVec2 A(cx - 28, cy + 2), B(cx - 6, cy + 24), C(cx + 30, cy - 20);
            fg->AddLine(A, B, IM_COL32(255, 255, 255, (int)(255 * a)), th);
            fg->AddLine(B, C, IM_COL32(255, 255, 255, (int)(255 * a)), th);
        }
    }
}

// ============================================================
//  ТОСТЫ
// ============================================================
static void DrawToasts() {
    ThemeColors t = GetTheme();
    float dt = ImGui::GetIO().DeltaTime; if (dt <= 0.0f) dt = 0.016f;
    for (auto& tt : g_toasts) tt.timer += dt;
    while (!g_toasts.empty() && g_toasts.front().timer > 2.5f) g_toasts.pop_front();

    ImDrawList* fg = ImGui::GetForegroundDrawList();
    ImVec2 scr = ImGui::GetIO().DisplaySize;
    float y = 80.0f;
    for (size_t i = 0; i < g_toasts.size(); ++i) {
        auto& tt = g_toasts[i];
        float a = 1.0f;
        if (tt.timer < 0.2f) a = tt.timer / 0.2f;
        else if (tt.timer > 2.0f) a = 1.0f - (tt.timer - 2.0f) / 0.5f;
        if (a < 0.01f) { y += 52; continue; }

        ImVec2 ts = ImGui::CalcTextSize(tt.text.c_str());
        float w = ts.x + 60.0f, h = 40.0f;
        ImVec2 p0(scr.x - w - 24.0f, y);
        ImVec2 p1(p0.x + w, p0.y + h);

        ImU32 bg = ColF(t.window_bg, 0.95f * a);
        ImU32 bd = tt.is_achievement ? IM_COL32(255, 200, 60, (int)(255 * a)) : ColF(t.accent, 0.8f * a);
        ImU32 accent = tt.is_achievement ? IM_COL32(255, 200, 60, (int)(255 * a)) : ColF(t.accent, a);

        fg->AddRectFilled(p0, p1, bg, 10.0f);
        fg->AddRect(p0, p1, bd, 10.0f, 0, 1.5f);

        ImVec2 c(p0.x + 22.0f, p0.y + h * 0.5f);
        fg->AddCircleFilled(c, 11.0f, tt.is_achievement
            ? IM_COL32(255, 200, 60, (int)(60 * a))
            : ColF(t.accent, 0.25f * a));

        if (tt.is_achievement) {
            ImVec2 pts[10];
            float R = 7.0f, r = R * 0.45f;
            for (int k = 0; k < 10; k++) {
                float ang = -1.5707963f + k * 3.1415926f / 5.0f;
                float rr = (k % 2 == 0) ? R : r;
                pts[k] = ImVec2(c.x + std::cos(ang) * rr, c.y + std::sin(ang) * rr);
            }
            fg->AddConvexPolyFilled(pts, 10, accent);
        }
        else {
            fg->AddLine(ImVec2(c.x - 5, c.y), ImVec2(c.x - 1, c.y + 4), accent, 2.0f);
            fg->AddLine(ImVec2(c.x - 1, c.y + 4), ImVec2(c.x + 6, c.y - 5), accent, 2.0f);
        }

        fg->AddText(ImVec2(p0.x + 42.0f, p0.y + (h - ts.y) * 0.5f), ColF(t.text, a), tt.text.c_str());
        y += h + 10.0f;
    }
}

// ============================================================
//  SPLASH
// ============================================================
static void DrawSplashScreen() {
    ImGuiIO& io = ImGui::GetIO();
    ImDrawList* fg = ImGui::GetForegroundDrawList();
    ThemeColors t = GetTheme();
    float dt = io.DeltaTime; if (dt <= 0.0f) dt = 0.016f;
    g_splash.timer += dt;
    bool can_hide = g_splash.ready && g_splash.timer >= g_splash.min_show_time;
    float ta = can_hide ? 0.0f : 1.0f;
    g_splash.alpha += (ta - g_splash.alpha) * 5.5f * dt;
    if (g_splash.alpha < 0.005f && can_hide) { g_splash.active = false; g_splash.alpha = 0.0f; return; }
    float a = MY_CLAMP(g_splash.alpha, 0.0f, 1.0f);
    fg->AddRectFilled(ImVec2(0, 0), io.DisplaySize, IM_COL32(38, 36, 48, (int)(255 * a)));
    float cx = io.DisplaySize.x * 0.5f, cy = io.DisplaySize.y * 0.5f;
    const float R = 34.0f, th = 4.0f;
    fg->AddCircle(ImVec2(cx, cy), R, IM_COL32(255, 255, 255, (int)(30 * a)), 60, th);
    float ang = (float)(ImGui::GetTime() * 2.4f);
    for (int i = 0; i < 20; i++) {
        float t0 = ang + 3.6f * ((float)i / 20), t1 = ang + 3.6f * ((float)(i + 1) / 20);
        fg->PathClear(); fg->PathArcTo(ImVec2(cx, cy), R, t0, t1, 4);
        fg->PathStroke(ColF(t.accent, a * ((float)(i + 1) / 20)), 0, th);
    }
    ImVec2 head(cx + cosf(ang + 3.6f) * R, cy + sinf(ang + 3.6f) * R);
    fg->AddCircleFilled(head, th * 0.9f, ColF(t.accent, a));
    if (g_font_title) ImGui::PushFont(g_font_title);
    ImVec2 tsz = ImGui::CalcTextSize("SteamLuaTools");
    fg->AddText(ImVec2(cx - tsz.x * 0.5f, cy + R + 30.0f), ColF(t.text, a), "SteamLuaTools");
    if (g_font_title) ImGui::PopFont();
    ImVec2 ssz = ImGui::CalcTextSize(g_splash.step_text.c_str());
    fg->AddText(ImVec2(cx - ssz.x * 0.5f, cy + R + 62.0f), ColF(t.text_disabled, a), g_splash.step_text.c_str());
}

// ============================================================
//  DrawUI
// ============================================================
static void DrawUI() {
    const ImGuiViewport* vp = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(vp->WorkPos);
    ImGui::SetNextWindowSize(vp->WorkSize);
    ImGuiWindowFlags flags =
        ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
        ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoNavFocus |
        ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse;
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
    ImGui::Begin("SteamLuaTools", nullptr, flags);
    ImGui::PopStyleVar();

    if (!g_splash.active || g_splash.alpha < 0.9f) {
        ImVec2 origin = ImGui::GetWindowPos();
        ImVec2 size = ImGui::GetWindowSize();
        ImDrawList* dl = ImGui::GetWindowDrawList();
        float dt = ImGui::GetIO().DeltaTime; if (dt <= 0.0f) dt = 0.016f;
        g_app.time_accum += dt;
        DrawBackgroundFx(dl, origin, size, g_app.time_accum);
    }

    if (g_app.tab_fade < 1.0f) { g_app.tab_fade += ImGui::GetIO().DeltaTime * 10.0f; if (g_app.tab_fade > 1.0f) g_app.tab_fade = 1.0f; }
    g_app.app_open_anim = ExpLerp(g_app.app_open_anim, 1.0f, 6.0f, ImGui::GetIO().DeltaTime);
    float ta = EaseOutCubic(MY_CLAMP(g_app.app_open_anim, 0, 1)) * EaseOutCubic(MY_CLAMP(g_app.tab_fade, 0, 1));
    ImGui::PushStyleVar(ImGuiStyleVar_Alpha, ta);

    DrawTopBar();

    ImGui::BeginChild("##content", ImVec2(0, 0), false, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
    ImGui::BeginChild("##content_pad", ImVec2(0, 0), false, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);

    switch (g_app.current_tab) {
    case 0: DrawShopPage(); break;
    case 1: DrawFavoritesPage(); break;
    case 2: DrawAchievementsPage(); break;
    case 3: DrawMiniGamesPage(); break;
    case 4: DrawLibraryPage(); break;
    case 5: DrawToolsPage(); break;
    default: break;
    }

    DrawCustomGradientPopup();

    ImGui::EndChild();
    ImGui::PopStyleVar();
    ImGui::EndChild();
    ImGui::PopStyleVar();
    ImGui::End();

    DrawOverlayAndProgress();
    DrawToasts();
    ProcessPendingTextures();

    if (g_splash.active) DrawSplashScreen();
}

// ============================================================
//  Init
// ============================================================
static std::atomic<bool> g_init_done{ false };

static void BackgroundInitThread() {
    g_splash.step_text = "Загрузка настроек...";
    InitAchievements();
    LoadSettings();
    ApplyStyle();

    g_splash.step_text = "Поиск Steam...";
    g_app_steam_path = FindSteamPath();
    g_app.steam_found = !g_app_steam_path.empty();
    if (g_app.steam_found) LogPush("Steam найден: " + g_app_steam_path);
    else LogPush("Steam не найден в системе", true);

    g_splash.step_text = "Загрузка базы игр...";
    LoadGamesJson();
    LoadSettings();
    RefreshInstalledFlags();
    RefreshInstalledFiles();
    RebuildFiltered();

    // Проверка времени суток
    {
        time_t now = time(nullptr);
        std::tm tm; localtime_s(&tm, &now);
        if (tm.tm_hour >= 0 && tm.tm_hour < 5) UnlockAchievement("night_owl");
        if (tm.tm_hour >= 0 && tm.tm_hour < 6) UnlockAchievement("early_bird");
    }

    g_splash.step_text = "Готово";
    g_init_done = true;
}

// ============================================================
//  D3D11 + WndProc
// ============================================================
static bool CreateDeviceD3D(HWND hWnd) {
    DXGI_SWAP_CHAIN_DESC sd; ZeroMemory(&sd, sizeof(sd));
    sd.BufferCount = 2;
    sd.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    sd.BufferDesc.RefreshRate.Numerator = 60; sd.BufferDesc.RefreshRate.Denominator = 1;
    sd.Flags = DXGI_SWAP_CHAIN_FLAG_ALLOW_MODE_SWITCH;
    sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    sd.OutputWindow = hWnd; sd.SampleDesc.Count = 1; sd.Windowed = TRUE;
    sd.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;
    UINT flags = 0;
    D3D_FEATURE_LEVEL level;
    const D3D_FEATURE_LEVEL levels[2] = { D3D_FEATURE_LEVEL_11_0, D3D_FEATURE_LEVEL_10_0 };
    HRESULT res = D3D11CreateDeviceAndSwapChain(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, flags,
        levels, 2, D3D11_SDK_VERSION, &sd, &g_pSwapChain, &g_pd3dDevice, &level, &g_pd3dDeviceContext);
    if (res == DXGI_ERROR_UNSUPPORTED)
        res = D3D11CreateDeviceAndSwapChain(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, flags,
            levels, 2, D3D11_SDK_VERSION, &sd, &g_pSwapChain, &g_pd3dDevice, &level, &g_pd3dDeviceContext);
    if (FAILED(res)) return false;
    CreateRenderTarget();
    return true;
}
static void CleanupDeviceD3D() {
    CleanupRenderTarget();
    if (g_pSwapChain) { g_pSwapChain->Release(); g_pSwapChain = nullptr; }
    if (g_pd3dDeviceContext) { g_pd3dDeviceContext->Release(); g_pd3dDeviceContext = nullptr; }
    if (g_pd3dDevice) { g_pd3dDevice->Release(); g_pd3dDevice = nullptr; }
}
static void CreateRenderTarget() {
    ID3D11Texture2D* back = nullptr;
    g_pSwapChain->GetBuffer(0, IID_PPV_ARGS(&back));
    if (back) { g_pd3dDevice->CreateRenderTargetView(back, nullptr, &g_mainRenderTargetView); back->Release(); }
}
static void CleanupRenderTarget() {
    if (g_mainRenderTargetView) { g_mainRenderTargetView->Release(); g_mainRenderTargetView = nullptr; }
}

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND, UINT, WPARAM, LPARAM);

static LRESULT WINAPI WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    if (ImGui_ImplWin32_WndProcHandler(hWnd, msg, wParam, lParam)) return true;
    switch (msg) {
    case WM_NCCALCSIZE:
        if (wParam == TRUE) return 0;
        break;
    case WM_SIZE:
        if (wParam == SIZE_MINIMIZED) return 0;
        g_ResizeWidth = (UINT)LOWORD(lParam); g_ResizeHeight = (UINT)HIWORD(lParam);
        return 0;
    case WM_SYSCOMMAND:
        if ((wParam & 0xfff0) == SC_KEYMENU) return 0;
        break;
    case WM_DESTROY:
        ::PostQuitMessage(0);
        return 0;
    default: break;
    }
    return ::DefWindowProcW(hWnd, msg, wParam, lParam);
}

// ============================================================
//  wWinMain
// ============================================================
int APIENTRY wWinMain(_In_ HINSTANCE hInstance, _In_opt_ HINSTANCE, _In_ LPWSTR, _In_ int)
{
    WNDCLASSEXW wc = { sizeof(wc), CS_CLASSDC, WndProc, 0L, 0L,
                       GetModuleHandle(nullptr), nullptr, nullptr, nullptr,
                       nullptr, L"SteamLuaToolsUI", nullptr };
    wc.hbrBackground = CreateSolidBrush(RGB(38, 36, 48));
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    ::RegisterClassExW(&wc);

    int win_w = 1280, win_h = 800;
    int sw = GetSystemMetrics(SM_CXSCREEN);
    int sh = GetSystemMetrics(SM_CYSCREEN);
    int px = (sw - win_w) / 2;
    int py = (sh - win_h) / 2;

    HWND hwnd = ::CreateWindowW(wc.lpszClassName, L"SteamLuaTools 9.5",
        WS_POPUP | WS_MINIMIZEBOX, px, py, win_w, win_h,
        nullptr, nullptr, wc.hInstance, nullptr);
    g_hwnd = hwnd;

    ApplyAppIcon(hwnd);
    BOOL dark = TRUE;
    DwmSetWindowAttribute(hwnd, 20, &dark, sizeof(dark));

    ::ShowWindow(hwnd, SW_SHOW);
    ::UpdateWindow(hwnd);

    if (!CreateDeviceD3D(hwnd)) {
        CleanupDeviceD3D();
        ::UnregisterClassW(wc.lpszClassName, wc.hInstance);
        return 1;
    }

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    io.IniFilename = nullptr;

    LoadFonts();
    ApplyStyle();

    ImGui_ImplWin32_Init(hwnd);
    ImGui_ImplDX11_Init(g_pd3dDevice, g_pd3dDeviceContext);

    g_splash.active = true;
    g_splash.ready = false;
    g_splash.timer = 0.0f;
    g_splash.alpha = 1.0f;
    g_splash.step_text = "Инициализация...";
    g_init_done = false;

    std::thread(BackgroundInitThread).detach();

    bool done = false;
    while (!done) {
        MSG msg;
        while (::PeekMessage(&msg, nullptr, 0U, 0U, PM_REMOVE)) {
            ::TranslateMessage(&msg); ::DispatchMessage(&msg);
            if (msg.message == WM_QUIT) done = true;
        }
        if (done) break;

        if (g_ResizeWidth != 0 && g_ResizeHeight != 0) {
            CleanupRenderTarget();
            g_pSwapChain->ResizeBuffers(0, g_ResizeWidth, g_ResizeHeight, DXGI_FORMAT_UNKNOWN, 0);
            g_ResizeWidth = g_ResizeHeight = 0;
            CreateRenderTarget();
        }

        ImGui_ImplDX11_NewFrame();
        ImGui_ImplWin32_NewFrame();
        ImGui::NewFrame();

        if (g_init_done.load()) g_splash.ready = true;

        DrawUI();

        ImGui::Render();
        ThemeColors tc = GetTheme();
        const float clear[4] = { tc.window_bg.x, tc.window_bg.y, tc.window_bg.z, 1.0f };
        g_pd3dDeviceContext->OMSetRenderTargets(1, &g_mainRenderTargetView, nullptr);
        g_pd3dDeviceContext->ClearRenderTargetView(g_mainRenderTargetView, clear);
        ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());

        g_pSwapChain->Present(1, 0);
    }

    ImGui_ImplDX11_Shutdown();
    ImGui_ImplWin32_Shutdown();
    ImGui::DestroyContext();
    for (auto& g : g_app.all_games) if (g.texture) g.texture->Release();
    CleanupDeviceD3D();
    ::DestroyWindow(hwnd);
    ::UnregisterClassW(wc.lpszClassName, wc.hInstance);
    return 0;
}

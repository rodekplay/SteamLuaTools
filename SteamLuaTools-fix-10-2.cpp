// ============================================================
//  SteamLuaTools 10.2
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
#include <tlhelp32.h>
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
#include <cstring>

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

static HWND g_hwnd = nullptr;
static std::string g_app_steam_path;

static ID3D11ShaderResourceView* g_icon_small_srv = nullptr;
static int g_icon_small_w = 0, g_icon_small_h = 0;
static ID3D11ShaderResourceView* g_logo_srv = nullptr;
static int g_logo_w = 0, g_logo_h = 0;

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
    if (h) {
        void* p = GlobalLock(h);
        if (p) {
            memcpy(p, text.c_str(), sz);
            GlobalUnlock(h);
            SetClipboardData(CF_TEXT, h);
        }
        else {
            GlobalFree(h);
        }
    }
    CloseClipboard();
}

// ============================================================
//  Нативные аналоги RunHidden / taskkill (без cmd/powershell)
// ============================================================
static bool KillProcessByNameW(const wchar_t* exeName) {
    bool any = false;
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snap == INVALID_HANDLE_VALUE) return false;
    PROCESSENTRY32W pe = {}; pe.dwSize = sizeof(pe);
    if (Process32FirstW(snap, &pe)) {
        do {
            if (_wcsicmp(pe.szExeFile, exeName) == 0) {
                HANDLE hProc = OpenProcess(PROCESS_TERMINATE, FALSE, pe.th32ProcessID);
                if (hProc) {
                    if (TerminateProcess(hProc, 0)) any = true;
                    CloseHandle(hProc);
                }
            }
        } while (Process32NextW(snap, &pe));
    }
    CloseHandle(snap);
    return any;
}

// ============================================================
//  Безопасная распаковка через системный tar.exe
// ============================================================
static bool ExtractZipNative(const std::string& zipPath, const std::string& outDir) {
    std::error_code ec;
    fs::create_directories(outDir, ec);
    std::wstring wZip = fs::absolute(zipPath).wstring();
    std::wstring wOut = fs::absolute(outDir).wstring();
    std::wstring cmd = L"tar.exe -xf \"" + wZip + L"\" -C \"" + wOut + L"\"";
    STARTUPINFOW si = { sizeof(si) };
    PROCESS_INFORMATION pi = { 0 };
    si.dwFlags = STARTF_USESHOWWINDOW;
    si.wShowWindow = SW_HIDE;
    if (CreateProcessW(nullptr, (wchar_t*)cmd.c_str(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi)) {
        WaitForSingleObject(pi.hProcess, INFINITE);
        DWORD exitCode = 0;
        GetExitCodeProcess(pi.hProcess, &exitCode);
        CloseHandle(pi.hProcess);
        CloseHandle(pi.hThread);
        return exitCode == 0;
    }
    return false;
}

// ============================================================
//  Шрифты
// ============================================================
static ImFont* g_font_regular = nullptr;
static ImFont* g_font_big = nullptr;
static ImFont* g_font_title = nullptr;
static ImFont* g_font_small = nullptr;
static ImFont* g_font_huge = nullptr;

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
            g_font_title = io.Fonts->AddFontFromFileTTF(path, 18.0f, &cfg, ranges);
            g_font_huge = io.Fonts->AddFontFromFileTTF(path, 30.0f, &cfg, ranges);
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
    ImVec4 window_bg, topbar_bg, sidebar_bg;
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
        t.window_bg = ImVec4(0.024f, 0.035f, 0.078f, 1); t.topbar_bg = ImVec4(0.018f, 0.026f, 0.058f, 1);
        t.accent = ImVec4(0.35f, 0.60f, 1.00f, 1); t.accent2 = ImVec4(0.60f, 0.80f, 1.00f, 1);
        t.accent3 = ImVec4(0.20f, 0.45f, 0.90f, 1); t.accent_hover = ImVec4(0.45f, 0.70f, 1.00f, 1);
        t.accent_active = ImVec4(0.25f, 0.50f, 0.90f, 1); break;
    case ThemeMode::Purple:
        t.window_bg = ImVec4(0.039f, 0.027f, 0.063f, 1); t.topbar_bg = ImVec4(0.028f, 0.020f, 0.048f, 1);
        t.accent = ImVec4(0.627f, 0.420f, 1.000f, 1); t.accent2 = ImVec4(0.980f, 0.400f, 0.650f, 1);
        t.accent3 = ImVec4(0.380f, 0.700f, 1.000f, 1); t.accent_hover = ImVec4(0.72f, 0.53f, 1.00f, 1);
        t.accent_active = ImVec4(0.52f, 0.33f, 0.90f, 1); break;
    case ThemeMode::Green:
        t.window_bg = ImVec4(0.024f, 0.063f, 0.039f, 1); t.topbar_bg = ImVec4(0.018f, 0.048f, 0.028f, 1);
        t.accent = ImVec4(0.30f, 0.85f, 0.50f, 1); t.accent2 = ImVec4(0.60f, 1.00f, 0.70f, 1);
        t.accent3 = ImVec4(0.15f, 0.60f, 0.35f, 1); t.accent_hover = ImVec4(0.40f, 0.95f, 0.60f, 1);
        t.accent_active = ImVec4(0.20f, 0.70f, 0.40f, 1); break;
    case ThemeMode::Red:
        t.window_bg = ImVec4(0.063f, 0.024f, 0.035f, 1); t.topbar_bg = ImVec4(0.048f, 0.018f, 0.028f, 1);
        t.accent = ImVec4(1.00f, 0.35f, 0.40f, 1); t.accent2 = ImVec4(1.00f, 0.60f, 0.60f, 1);
        t.accent3 = ImVec4(0.80f, 0.20f, 0.30f, 1); t.accent_hover = ImVec4(1.00f, 0.45f, 0.50f, 1);
        t.accent_active = ImVec4(0.85f, 0.25f, 0.30f, 1); break;
    case ThemeMode::Orange:
        t.window_bg = ImVec4(0.063f, 0.039f, 0.016f, 1); t.topbar_bg = ImVec4(0.048f, 0.030f, 0.012f, 1);
        t.accent = ImVec4(1.00f, 0.60f, 0.20f, 1); t.accent2 = ImVec4(1.00f, 0.80f, 0.50f, 1);
        t.accent3 = ImVec4(0.85f, 0.45f, 0.10f, 1); t.accent_hover = ImVec4(1.00f, 0.70f, 0.30f, 1);
        t.accent_active = ImVec4(0.85f, 0.50f, 0.15f, 1); break;
    case ThemeMode::Pink:
        t.window_bg = ImVec4(0.078f, 0.027f, 0.063f, 1); t.topbar_bg = ImVec4(0.060f, 0.020f, 0.048f, 1);
        t.accent = ImVec4(1.00f, 0.45f, 0.75f, 1); t.accent2 = ImVec4(1.00f, 0.70f, 0.85f, 1);
        t.accent3 = ImVec4(0.85f, 0.35f, 0.65f, 1); t.accent_hover = ImVec4(1.00f, 0.55f, 0.85f, 1);
        t.accent_active = ImVec4(0.85f, 0.35f, 0.65f, 1); break;
    case ThemeMode::Teal:
        t.window_bg = ImVec4(0.016f, 0.063f, 0.063f, 1); t.topbar_bg = ImVec4(0.012f, 0.048f, 0.048f, 1);
        t.accent = ImVec4(0.20f, 0.90f, 0.90f, 1); t.accent2 = ImVec4(0.60f, 1.00f, 1.00f, 1);
        t.accent3 = ImVec4(0.15f, 0.70f, 0.70f, 1); t.accent_hover = ImVec4(0.30f, 1.00f, 1.00f, 1);
        t.accent_active = ImVec4(0.15f, 0.75f, 0.75f, 1); break;
    case ThemeMode::Yellow:
        t.window_bg = ImVec4(0.063f, 0.063f, 0.016f, 1); t.topbar_bg = ImVec4(0.048f, 0.048f, 0.012f, 1);
        t.accent = ImVec4(0.95f, 0.90f, 0.30f, 1); t.accent2 = ImVec4(1.00f, 0.95f, 0.60f, 1);
        t.accent3 = ImVec4(0.80f, 0.75f, 0.20f, 1); t.accent_hover = ImVec4(1.00f, 0.95f, 0.40f, 1);
        t.accent_active = ImVec4(0.80f, 0.75f, 0.20f, 1); break;
    case ThemeMode::Gray:
        t.window_bg = ImVec4(0.055f, 0.055f, 0.055f, 1); t.topbar_bg = ImVec4(0.040f, 0.040f, 0.040f, 1);
        t.accent = ImVec4(0.65f, 0.65f, 0.65f, 1); t.accent2 = ImVec4(0.85f, 0.85f, 0.85f, 1);
        t.accent3 = ImVec4(0.45f, 0.45f, 0.45f, 1); t.accent_hover = ImVec4(0.80f, 0.80f, 0.80f, 1);
        t.accent_active = ImVec4(0.55f, 0.55f, 0.55f, 1); break;
    case ThemeMode::Sunset:
        t.window_bg = ImVec4(0.078f, 0.027f, 0.043f, 1); t.topbar_bg = ImVec4(0.060f, 0.020f, 0.032f, 1);
        t.accent = ImVec4(1.00f, 0.55f, 0.25f, 1); t.accent2 = ImVec4(1.00f, 0.40f, 0.55f, 1);
        t.accent3 = ImVec4(0.85f, 0.40f, 0.15f, 1); t.accent_hover = ImVec4(1.00f, 0.35f, 0.55f, 1);
        t.accent_active = ImVec4(0.85f, 0.40f, 0.15f, 1); break;
    case ThemeMode::Ocean:
        t.window_bg = ImVec4(0.016f, 0.039f, 0.078f, 1); t.topbar_bg = ImVec4(0.012f, 0.028f, 0.058f, 1);
        t.accent = ImVec4(0.20f, 0.60f, 0.95f, 1); t.accent2 = ImVec4(0.40f, 0.80f, 1.00f, 1);
        t.accent3 = ImVec4(0.15f, 0.45f, 0.85f, 1); t.accent_hover = ImVec4(0.30f, 0.75f, 1.00f, 1);
        t.accent_active = ImVec4(0.15f, 0.45f, 0.85f, 1); break;
    case ThemeMode::Forest:
        t.window_bg = ImVec4(0.027f, 0.055f, 0.027f, 1); t.topbar_bg = ImVec4(0.020f, 0.040f, 0.020f, 1);
        t.accent = ImVec4(0.40f, 0.80f, 0.35f, 1); t.accent2 = ImVec4(0.70f, 1.00f, 0.60f, 1);
        t.accent3 = ImVec4(0.30f, 0.65f, 0.25f, 1); t.accent_hover = ImVec4(0.55f, 0.95f, 0.45f, 1);
        t.accent_active = ImVec4(0.30f, 0.65f, 0.25f, 1); break;
    case ThemeMode::Custom:
    default:
        t.window_bg = ImVec4(0.035f, 0.024f, 0.058f, 1); t.topbar_bg = ImVec4(0.025f, 0.018f, 0.042f, 1);
        t.accent = ImVec4(g_custom_color_a[0], g_custom_color_a[1], g_custom_color_a[2], 1.0f);
        t.accent2 = ImVec4(g_custom_color_b[0], g_custom_color_b[1], g_custom_color_b[2], 1.0f);
        t.accent3 = ImVec4(g_custom_color_a[0] * 0.7f, g_custom_color_a[1] * 0.7f, g_custom_color_a[2] * 0.7f, 1.0f);
        t.accent_hover = ImVec4(MY_CLAMP(g_custom_color_a[0] * 1.15f, 0, 1), MY_CLAMP(g_custom_color_a[1] * 1.15f, 0, 1), MY_CLAMP(g_custom_color_a[2] * 1.15f, 0, 1), 1);
        t.accent_active = ImVec4(MY_CLAMP(g_custom_color_a[0] * 0.85f, 0, 1), MY_CLAMP(g_custom_color_a[1] * 0.85f, 0, 1), MY_CLAMP(g_custom_color_a[2] * 0.85f, 0, 1), 1);
        break;
    }
    t.sidebar_bg = t.topbar_bg;
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
static void CloseSteam() {
    LogPush("Закрываю Steam...");
    KillProcessByNameW(L"steam.exe");
    KillProcessByNameW(L"steamwebhelper.exe");
    Sleep(1200);
    LogPush("Steam закрыт");
}
static void RestartSteam() {
    if (g_app_steam_path.empty()) { LogPush("Перезапуск невозможен: Steam не найден", true); return; }
    LogPush("Перезапуск Steam: " + g_app_steam_path);
    CloseSteam();
    ShellExecuteA(nullptr, "open", (g_app_steam_path + "\\Steam.exe").c_str(), nullptr, nullptr, SW_SHOWNORMAL);
    LogPush("Steam запущен");
}
static void OpenLuaFolder() {
    if (g_app_steam_path.empty()) { LogPush("Папка Lua недоступна: Steam не найден", true); return; }
    fs::path lua = fs::path(g_app_steam_path) / "config" / "lua";
    std::error_code ec; fs::create_directories(lua, ec);
    LogPush("Открываю папку: " + lua.string());
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
    HINTERNET hSession = WinHttpOpen(L"SLT/10.2", WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY, WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
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
//  AppState
// ============================================================
struct GameEntry {
    std::string appid, name, header_image;
    std::vector<std::string> tags;
    bool nsfw = false;
    ID3D11ShaderResourceView* texture = nullptr;
    bool loading = false, load_failed = false;
    float fade_in = 0.0f;
    float open_time = -1.0f;
    bool installed = false;
    float copy_hover_t = 0.0f;
    float steam_hover_t = 0.0f;
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
    float tab_fade = 1.0f;
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

static bool IsInputBlocked() {
    return g_app.IsBusy() || g_app.success_visible;
}

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
        j["custom_a"] = { g_custom_color_a[0], g_custom_color_a[1], g_custom_color_a[2], g_custom_color_a[3] };
        j["custom_b"] = { g_custom_color_b[0], g_custom_color_b[1], g_custom_color_b[2], g_custom_color_b[3] };

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
        if (j.contains("custom_a") && j["custom_a"].is_array() && j["custom_a"].size() == 4)
            for (int i = 0; i < 4; ++i) g_custom_color_a[i] = j["custom_a"][i].get<float>();
        if (j.contains("custom_b") && j["custom_b"].is_array() && j["custom_b"].size() == 4)
            for (int i = 0; i < 4; ++i) g_custom_color_b[i] = j["custom_b"][i].get<float>();
    }
    catch (...) {}
}

// ============================================================
//  Стиль (все скругления = 0)
// ============================================================
static void ApplyStyle() {
    ThemeColors t = GetTheme();
    ImGuiStyle& s = ImGui::GetStyle();
    s.WindowRounding = 0.0f; s.ChildRounding = 0.0f; s.FrameRounding = 0.0f;
    s.PopupRounding = 0.0f; s.ScrollbarRounding = 0.0f; s.GrabRounding = 0.0f;
    s.TabRounding = 0.0f;
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
        if (raw.size() < 8 || raw.size() > 20 * 1024 * 1024) {
            g_app.active_downloads--;
            return;
        }

        int w = 0, h = 0, comp = 0;
        unsigned char* pixels = nullptr;
        try {
            pixels = stbi_load_from_memory(raw.data(), (int)raw.size(), &w, &h, &comp, 4);
        }
        catch (...) {
            pixels = nullptr;
        }

        if (pixels) {
            if (w <= 0 || h <= 0 || (size_t)w * h > 4096 * 4096) {
                stbi_image_free(pixels);
                g_app.active_downloads--;
                return;
            }
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
    if (g_app.active_downloads.load() >= 32) return;
    g.loading = true; g_app.active_downloads++;
    std::thread(DownloadAndDecodeWorker, g.header_image, g.appid).detach();
}
static void ProcessPendingTextures() {
    if (!g_pd3dDevice || !g_pd3dDeviceContext) return;
    int done = 0;
    while (done < 8) {
        PendingTexture pt;
        {
            std::lock_guard<std::mutex> lock(g_app.pending_mutex); if (g_app.pending_textures.empty()) break;
            pt = std::move(g_app.pending_textures.front()); g_app.pending_textures.pop_front();
        }
        if (pt.pixels.empty()) { done++; continue; }
        if (pt.w <= 0 || pt.h <= 0 || pt.pixels.empty()) { done++; continue; }
        D3D11_TEXTURE2D_DESC desc = {};
        desc.Width = pt.w; desc.Height = pt.h; desc.MipLevels = 1; desc.ArraySize = 1;
        desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM; desc.SampleDesc.Count = 1;
        desc.Usage = D3D11_USAGE_DEFAULT; desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
        D3D11_SUBRESOURCE_DATA sub = {}; sub.pSysMem = pt.pixels.data(); sub.SysMemPitch = pt.w * 4;
        ID3D11Texture2D* tex = nullptr;
        if (SUCCEEDED(g_pd3dDevice->CreateTexture2D(&desc, &sub, &tex))) {
            ID3D11ShaderResourceView* srv = nullptr;
            if (tex && SUCCEEDED(g_pd3dDevice->CreateShaderResourceView(tex, nullptr, &srv))) {
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
    LogPush("Фильтр: '" + q + "' | тег: " + (tag_is_all ? "все" : tag_value) + " | найдено: " + std::to_string(g_app.filtered.size()));
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
    if (g_app_steam_path.empty()) { LogPush("Удаление невозможно: Steam не найден", true); return; }
    fs::path lua = fs::path(g_app_steam_path) / "config" / "lua";
    fs::path depot = fs::path(g_app_steam_path) / "depotcache";
    std::error_code ec;
    int removed_lua = 0, removed_depot = 0;

    if (fs::exists(lua)) for (auto& p : fs::directory_iterator(lua)) if (p.path().stem().string() == appid) {
        LogPush("Удаляю: " + p.path().string());
        fs::remove(p.path(), ec);
        if (!ec) removed_lua++;
    }
    if (fs::exists(depot)) for (auto& p : fs::directory_iterator(depot)) if (p.path().stem().string() == appid) {
        LogPush("Удаляю: " + p.path().string());
        fs::remove(p.path(), ec);
        if (!ec) removed_depot++;
    }
    LogPush("Удаление AppID " + appid + ": lua=" + std::to_string(removed_lua) + ", depot=" + std::to_string(removed_depot));
    RefreshInstalledFiles(); RefreshInstalledFlags();
}
static int CopyLuaFromDir(const std::string& extract_dir) {
    fs::path lua_dst = fs::path(g_app_steam_path) / "config" / "lua";
    fs::path depot_dst = fs::path(g_app_steam_path) / "depotcache";
    fs::create_directories(lua_dst); fs::create_directories(depot_dst);
    LogPush("Копирование в lua: " + lua_dst.string());
    LogPush("Копирование в depotcache: " + depot_dst.string());
    int copied = 0;
    for (auto& p : fs::recursive_directory_iterator(extract_dir)) {
        if (!p.is_regular_file()) continue;
        auto ext = p.path().extension().string();
        if (ext == ".lua" || ext == ".manifest") {
            std::error_code ec;
            if (fs::file_size(p.path(), ec) > 5 * 1024 * 1024) continue;
            try { fs::copy_file(p.path(), lua_dst / p.path().filename(), fs::copy_options::overwrite_existing, ec); }
            catch (...) { continue; }
            if (ec) LogPush("Не удалось скопировать " + p.path().string() + " -> lua: " + ec.message(), true);
            try { fs::copy_file(p.path(), depot_dst / p.path().filename(), fs::copy_options::overwrite_existing, ec); }
            catch (...) { continue; }
            if (ec) LogPush("Не удалось скопировать " + p.path().string() + " -> depotcache: " + ec.message(), true);
            LogPush("Скопирован файл: " + p.path().filename().string());
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

        LogPush("URL: " + url);
        LogPush("Временный ZIP: " + zip_path);
        LogPush("Папка распаковки: " + extract_dir);

        auto cleanup_partial = [&]() {
            std::error_code ec;
            LogPush("Очистка временных: " + zip_path + ", " + extract_dir);
            fs::remove(zip_path, ec); fs::remove_all(extract_dir, ec);
            };

        g_app.progress_target = 0.15f;
        bool ok = DownloadToFile(url, zip_path);
        if (ok) {
            std::error_code ec;
            auto sz = fs::file_size(zip_path, ec);
            if (!ec) LogPush("ZIP сохранён: " + std::to_string(sz) + " байт");
        }
        if (g_app.cancel_requested) {
            cleanup_partial(); LogPush("Загрузка отменена: " + name, true);
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

        bool unzip_ok = ExtractZipNative(zip_path, extract_dir);
        if (!unzip_ok) LogPush("tar.exe не смог распаковать ZIP", true);

        if (g_app.cancel_requested) {
            cleanup_partial(); LogPush("Загрузка отменена: " + name, true);
            g_app.progress_mode.store((int)ProgressMode::None); g_app.progress_target = 0.0f; g_app.progress_display = 0.0f; return;
        }
        g_app.progress_target = 0.88f;
        int copied = CopyLuaFromDir(extract_dir);
        LogPush("Скопировано файлов: " + std::to_string(copied) + " для " + name);
        if (copied == 0) LogPush("В архиве не найдено ни одного .lua или .manifest!", true);
        cleanup_partial();

        g_app.progress_target = 1.0f;
        std::this_thread::sleep_for(std::chrono::milliseconds(300));
        RefreshInstalledFlags(); RefreshInstalledFiles();

        g_app.progress_mode.store((int)ProgressMode::None); g_app.progress_target = 0.0f; g_app.progress_display = 0.0f;
        ShowSuccess();
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
        LogPush("Отмена загрузки AppID " + appid);
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
        LogPush("Отмена: удалено файлов " + std::to_string(total) + " для AppID " + appid);
        RefreshInstalledFiles(); RefreshInstalledFlags();
        g_app.progress_target = 1.0f; std::this_thread::sleep_for(std::chrono::milliseconds(400));
        g_app.progress_mode.store((int)ProgressMode::None); g_app.progress_target = 0.0f; g_app.progress_display = 0.0f;
        ShowSuccess();
        }).detach();
}

static void InstallZipFile(const std::string& zip_path) {
    if (g_app_steam_path.empty()) { LogPush("Установка ZIP невозможна: Steam не найден", true); return; }
    LogPush("Установка ZIP вручную: " + zip_path);
    char temp_path[MAX_PATH]; GetTempPathA(MAX_PATH, temp_path);
    std::string extract_dir = std::string(temp_path) + "SLT_manual";
    LogPush("Временная папка: " + extract_dir);
    std::error_code ec; fs::remove_all(extract_dir, ec);
    bool unzip_ok = ExtractZipNative(zip_path, extract_dir);
    if (!unzip_ok) LogPush("tar.exe не смог распаковать ZIP", true);
    int copied = CopyLuaFromDir(extract_dir);
    LogPush("Из ZIP скопировано файлов: " + std::to_string(copied));
    if (copied == 0) LogPush("В ZIP не найдено ни одного .lua или .manifest!", true);
    fs::remove_all(extract_dir, ec);
    RefreshInstalledFiles(); RefreshInstalledFlags();
    LogPush("Установка ZIP завершена");
}

static void InstallOpenSteamToolsAsync() {
    if (g_app.IsBusy()) { LogPush("Уже идёт операция", true); return; }
    g_app.progress_mode.store((int)ProgressMode::InstallTools);
    g_app.progress_target = 0.05f; g_app.progress_display = 0.0f;
    { std::lock_guard<std::mutex> lk(g_app.status_mtx); g_app.current_name = "OpenSteamTools"; g_app.current_appid = ""; }
    LogPush("Установка OpenSteamTools...");

    std::thread([]() {
        LogPush("Поиск OpenSteamTools.zip...");
        const char* paths[] = { "OpenSteamTools.zip", "..\\OpenSteamTools.zip" };
        std::string zip;
        for (auto p : paths) if (fs::exists(p)) { zip = p; LogPush("Найден файл: " + std::string(p)); break; }
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
            LogPush("OpenSteamTools.zip извлечён из ресурсов в: " + zip_path + " (" + std::to_string(size) + " байт)");
            zip = zip_path;
        }
        LogPush("Закрытие Steam...");
        g_app.progress_target = 0.20f; CloseSteam();
        g_app.progress_target = 0.45f;
        std::error_code ec; fs::remove_all(extract_dir, ec);
        bool unzip_ok = ExtractZipNative(fs::absolute(zip).string(), extract_dir);
        if (!unzip_ok) LogPush("tar.exe не смог распаковать OpenSteamTools.zip", true);
        g_app.progress_target = 0.70f;
        int copied = 0;
        LogPush("Копирование OpenSteamTools в: " + g_app_steam_path);
        for (auto& p : fs::recursive_directory_iterator(extract_dir)) {
            if (!p.is_regular_file()) continue;
            try {
                fs::path dst = fs::path(g_app_steam_path) / p.path().filename();
                fs::copy_file(p.path(), dst, fs::copy_options::overwrite_existing);
                LogPush("Скопирован: " + p.path().filename().string());
                copied++;
            }
            catch (std::exception& e) {
                LogPush("Ошибка копирования " + p.path().filename().string() + ": " + e.what(), true);
            }
        }
        fs::remove_all(extract_dir, ec);
        if (zip == zip_path) fs::remove(zip_path, ec);
        g_app.progress_target = 0.95f;
        LogPush("OpenSteamTools: скопировано " + std::to_string(copied) + " файлов");
        std::this_thread::sleep_for(std::chrono::milliseconds(300));
        g_app.progress_target = 1.0f; std::this_thread::sleep_for(std::chrono::milliseconds(400));
        g_app.progress_mode.store((int)ProgressMode::None); g_app.progress_target = 0.0f; g_app.progress_display = 0.0f;
        ShowSuccess();
        }).detach();
}

// ============================================================
//  FX (все фигуры без скруглений)
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
            float sway = std::sin(g_app.time_accum * 1.5f + p.phase) * 6.0f;
            float alpha = 0.55f + 0.35f * std::sin(g_app.time_accum * 2.0f + p.phase);
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
            float sway = std::sin(g_app.time_accum * 0.8f + p.phase) * 30.0f;
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
//  Кнопки окна (без скруглений)
// ============================================================
static void DrawWindowButton(ImDrawList* dl, ImVec2 p0, ImVec2 p1, bool is_close, bool hovered, bool active) {
    ImVec4 base = is_close ? ImVec4(0.85f, 0.22f, 0.32f, 1) : ImVec4(1, 1, 1, 1);
    float bg_alpha = active ? (is_close ? 1.0f : 0.22f) : (hovered ? (is_close ? 0.9f : 0.14f) : 0.0f);
    dl->AddRectFilled(p0, p1, ColF(base, bg_alpha), 0.0f);
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
//  Иконки вкладок
// ============================================================
static void DrawTabIcon(ImDrawList* dl, int tab_idx, float cx, float cy, bool active, bool hovered, ThemeColors& t) {
    float R = 14.0f;

    ImVec4 base_col;
    if (active) {
        base_col = t.accent;
    }
    else if (hovered) {
        base_col = ImVec4(0.80f, 0.79f, 0.85f, 1.0f);
    }
    else {
        base_col = ImVec4(0.50f, 0.49f, 0.56f, 1.0f);
    }

    ImVec4 hi_col = ImVec4(
        MY_CLAMP(base_col.x + 0.20f, 0.0f, 1.0f),
        MY_CLAMP(base_col.y + 0.20f, 0.0f, 1.0f),
        MY_CLAMP(base_col.z + 0.20f, 0.0f, 1.0f), 1.0f);
    ImVec4 lo_col = ImVec4(base_col.x * 0.50f, base_col.y * 0.50f, base_col.z * 0.50f, 1.0f);

    ImU32 c_main = IM_COL32((int)(base_col.x * 255), (int)(base_col.y * 255), (int)(base_col.z * 255), 255);
    ImU32 c_hi = IM_COL32((int)(hi_col.x * 255), (int)(hi_col.y * 255), (int)(hi_col.z * 255), 255);
    ImU32 c_lo = IM_COL32((int)(lo_col.x * 255), (int)(lo_col.y * 255), (int)(lo_col.z * 255), 255);

    if (active) {
        dl->AddCircleFilled(ImVec2(cx, cy), R * 1.75f, ColF(t.accent, 0.10f), 40);
        dl->AddCircleFilled(ImVec2(cx, cy), R * 1.20f, ColF(t.accent, 0.10f), 40);
    }

    switch (tab_idx) {
    case 0: {
        if (g_icon_small_srv) {
            float sz = R * 2.4f;
            ImVec2 p0(cx - sz * 0.5f, cy - sz * 0.5f);
            ImVec2 p1(cx + sz * 0.5f, cy + sz * 0.5f);

            ImU32 tint;
            if (active)       tint = ColF(t.accent, 1.0f);
            else if (hovered) tint = IM_COL32(230, 228, 240, 255);
            else              tint = IM_COL32(150, 148, 162, 255);

            dl->AddImage((ImTextureID)g_icon_small_srv, p0, p1,
                ImVec2(0, 0), ImVec2(1, 1), tint);
        }
        else {
            dl->AddCircleFilled(ImVec2(cx, cy), R * 0.98f, c_main, 48);
        }
        break;
    }
    case 1: {
        float shelf_y = cy + R * 0.72f;

        struct Book { float x, w, h; };
        Book books[3] = {
            { cx - R * 0.72f, R * 0.40f, R * 1.10f },
            { cx - R * 0.22f, R * 0.44f, R * 1.42f },
            { cx + R * 0.28f, R * 0.40f, R * 1.22f },
        };

        for (int i = 0; i < 3; i++) {
            auto& b = books[i];
            float by = shelf_y - b.h;

            dl->AddRectFilled(ImVec2(b.x, by), ImVec2(b.x + b.w, shelf_y), c_main);

            dl->AddLine(ImVec2(b.x + 1.5f, by + 1.0f),
                ImVec2(b.x + b.w - 1.5f, by + 1.0f),
                c_hi, 1.1f);

            dl->AddLine(ImVec2(b.x + b.w - 1.0f, by + 2.0f),
                ImVec2(b.x + b.w - 1.0f, shelf_y - 1.0f),
                c_lo, 0.9f);
        }

        dl->AddRectFilled(
            ImVec2(cx - R * 0.95f, shelf_y),
            ImVec2(cx + R * 0.95f, shelf_y + 2.0f),
            c_main);
        dl->AddLine(
            ImVec2(cx - R * 0.95f, shelf_y + 2.0f),
            ImVec2(cx + R * 0.95f, shelf_y + 2.0f),
            c_lo, 1.0f);
        break;
    }
    case 2: {
        int teeth = 8;
        float r_in = R * 0.55f;
        float r_out = R * 0.95f;
        float half_tooth = 3.1415926f / teeth * 0.42f;
        for (int i = 0; i < teeth; i++) {
            float a = i * (2.0f * 3.1415926f / teeth) - 3.1415926f * 0.5f;
            float a0 = a - half_tooth;
            float a1 = a + half_tooth;
            ImVec2 p[4] = {
                ImVec2(cx + cosf(a0) * r_in,  cy + sinf(a0) * r_in),
                ImVec2(cx + cosf(a0) * r_out, cy + sinf(a0) * r_out),
                ImVec2(cx + cosf(a1) * r_out, cy + sinf(a1) * r_out),
                ImVec2(cx + cosf(a1) * r_in,  cy + sinf(a1) * r_in),
            };
            dl->AddConvexPolyFilled(p, 4, c_main);
        }
        dl->AddCircleFilled(ImVec2(cx, cy), R * 0.80f, c_main, 40);
        dl->PathClear();
        dl->PathArcTo(ImVec2(cx, cy), R * 0.74f, 3.1415926f * 1.15f, 3.1415926f * 1.85f, 24);
        dl->PathStroke(c_hi, 0, 1.5f);
        dl->PathClear();
        dl->PathArcTo(ImVec2(cx, cy), R * 0.74f, 3.1415926f * 0.15f, 3.1415926f * 0.85f, 24);
        dl->PathStroke(c_lo, 0, 1.2f);
        dl->AddCircleFilled(ImVec2(cx, cy), R * 0.32f, ColF(t.sidebar_bg, 1.0f), 24);
        dl->AddCircle(ImVec2(cx, cy), R * 0.32f, c_lo, 24, 1.5f);
        break;
    }
    case 3: {
        float w = R * 1.85f, h = R * 1.55f;
        ImVec2 w0(cx - w * 0.5f, cy - h * 0.5f);
        ImVec2 w1(cx + w * 0.5f, cy + h * 0.5f);
        dl->AddRectFilled(w0, w1, c_main);
        dl->AddLine(w0, ImVec2(w1.x, w0.y), c_hi, 1.2f);
        dl->AddLine(ImVec2(w0.x, w1.y), w1, c_lo, 1.2f);

        float bar_h = R * 0.42f;
        dl->AddRectFilled(w0, ImVec2(w1.x, w0.y + bar_h), c_lo);
        dl->AddLine(ImVec2(w0.x, w0.y + bar_h), ImVec2(w1.x, w0.y + bar_h), c_hi, 0.8f);

        float dot_y = w0.y + bar_h * 0.5f;
        float dot_r = 1.7f;
        dl->AddCircleFilled(ImVec2(w0.x + 5.5f, dot_y), dot_r, c_hi, 10);
        dl->AddCircleFilled(ImVec2(w0.x + 10.5f, dot_y), dot_r, c_hi, 10);
        dl->AddCircleFilled(ImVec2(w0.x + 15.5f, dot_y), dot_r, c_hi, 10);

        float y0 = w1.y - 9.5f;
        float y1v = w1.y - 6.5f;
        float y2 = w1.y - 3.5f;
        dl->AddLine(ImVec2(w0.x + 5.5f, y0), ImVec2(w0.x + 9.0f, y1v), c_hi, 1.7f);
        dl->AddLine(ImVec2(w0.x + 9.0f, y1v), ImVec2(w0.x + 5.5f, y2), c_hi, 1.7f);
        dl->AddLine(ImVec2(w0.x + 11.5f, y2), ImVec2(w0.x + 16.0f, y2), c_hi, 1.7f);
        break;
    }
    }
}

// ============================================================
//  Sidebar (вертикальные вкладки)
// ============================================================
static void DrawSidebar() {
    ThemeColors t = GetTheme();
    const float sidebar_w = 80.0f;

    ImGui::PushStyleColor(ImGuiCol_ChildBg, t.sidebar_bg);
    ImGui::BeginChild("##sidebar", ImVec2(sidebar_w, 0), false, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);

    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImVec2 wp = ImGui::GetWindowPos(), ws = ImGui::GetWindowSize();

    dl->AddLine(ImVec2(wp.x + ws.x - 1, wp.y), ImVec2(wp.x + ws.x - 1, wp.y + ws.y),
        ColF(t.accent, 0.25f), 1.0f);

    const float btn_w = 56.0f, btn_h = 56.0f, btn_gap = 10.0f;
    const float start_y = 16.0f;
    const float btn_x = (sidebar_w - btn_w) * 0.5f;

    struct TabInfo { const char* label; int idx; };
    TabInfo tabs[4] = {
    {"Магазин",0},{"Библиотека",1},{"Инструменты",2},{"Отладка",3}
    };

    static float s_under_y = -1.0f;
    float dt = ImGui::GetIO().DeltaTime; if (dt <= 0.0f) dt = 0.016f;
    ImVec2 mp = ImGui::GetIO().MousePos;

    bool busy = g_app.IsBusy();
    float target_under_y = 0.0f;
    bool got_active = false;

    for (int i = 0; i < 4; i++) {
        float bx = wp.x + btn_x;
        float by = wp.y + start_y + i * (btn_h + btn_gap);
        ImVec2 b0(bx, by), b1(bx + btn_w, by + btn_h);

        bool active = (g_app.current_tab == tabs[i].idx);
        bool hovered = mp.x >= b0.x && mp.x <= b1.x && mp.y >= b0.y && mp.y <= b1.y && !busy;

        if (active) {
            target_under_y = by + btn_h * 0.5f;
            got_active = true;
        }

        if (active) {
            dl->AddRectFilled(b0, b1, ColF(t.accent, 0.18f));
        }
        else if (hovered) {
            dl->AddRectFilled(b0, b1, ColF(t.accent, 0.08f));
        }

        float cx = (b0.x + b1.x) * 0.5f;
        float cy = (b0.y + b1.y) * 0.5f;
        DrawTabIcon(dl, tabs[i].idx, cx, cy, active, hovered, t);

        if (hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
            if (g_app.current_tab != tabs[i].idx) {
                g_app.prev_tab = g_app.current_tab;
                g_app.current_tab = tabs[i].idx;
                g_app.tab_fade = 1.0f;
            }
        }

        if (hovered) {
            ImDrawList* fg = ImGui::GetForegroundDrawList();
            ImVec2 ts = ImGui::CalcTextSize(tabs[i].label);
            ImVec2 tt0(b1.x + 10, cy - 14.0f);
            ImVec2 tt1(tt0.x + ts.x + 20, tt0.y + 28.0f);
            fg->AddRectFilled(tt0, tt1, IM_COL32(25, 22, 34, 245));
            fg->AddRect(tt0, tt1, ColF(t.accent, 0.7f), 0.0f, 0, 1.0f);
            fg->AddText(ImVec2(tt0.x + 10, tt0.y + 5), ColF(t.text, 1.0f), tabs[i].label);
        }
    }

    if (got_active) {
        if (s_under_y < 0.0f) s_under_y = target_under_y;
        s_under_y = ExpLerp(s_under_y, target_under_y, 14.0f, dt);

        float ind_h = 40.0f;
        float ind_x = wp.x + 4.0f;
        float ind_w = 3.0f;
        for (int i = 3; i >= 1; i--) {
            float e = (float)i * 2.0f;
            dl->AddRectFilled(ImVec2(ind_x - e * 0.5f, s_under_y - ind_h * 0.5f - e),
                ImVec2(ind_x + ind_w + e * 0.5f, s_under_y + ind_h * 0.5f + e),
                ColF(t.accent, 0.10f / i));
        }
        dl->AddRectFilled(ImVec2(ind_x, s_under_y - ind_h * 0.5f),
            ImVec2(ind_x + ind_w, s_under_y + ind_h * 0.5f),
            ColF(t.accent, 1.0f));
    }

    ImGui::EndChild();
    ImGui::PopStyleColor();
}

// ============================================================
//  Верхняя полоса над контентом
// ============================================================
static void DrawMainTopBar() {
    ThemeColors t = GetTheme();
    const float topbar_h = 44.0f;

    ImGui::PushStyleColor(ImGuiCol_ChildBg, t.topbar_bg);
    ImGui::BeginChild("##main_topbar", ImVec2(0, topbar_h), false, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);

    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImVec2 wp = ImGui::GetWindowPos(), ws = ImGui::GetWindowSize();

    dl->AddLine(ImVec2(wp.x, wp.y + ws.y - 1), ImVec2(wp.x + ws.x, wp.y + ws.y - 1),
        ColF(t.accent, 0.25f), 1.0f);

    static bool s_drag = false;
    static POINT s_m{}, s_w{};
    ImVec2 mp = ImGui::GetIO().MousePos;
    bool in_drag = mp.x >= wp.x && mp.x <= wp.x + ws.x - 100 && mp.y >= wp.y && mp.y <= wp.y + topbar_h;

    ImGui::SetCursorPos(ImVec2(20, 12));
    if (g_font_title) ImGui::PushFont(g_font_title);
    ImGui::TextColored(t.text, "SteamLuaTools 10.2");
    if (g_font_title) ImGui::PopFont();

    if (!s_drag && in_drag && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
        s_drag = true; GetCursorPos(&s_m); RECT rc; GetWindowRect(g_hwnd, &rc); s_w = { rc.left, rc.top };
    }
    if (s_drag && ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
        POINT p; GetCursorPos(&p);
        SetWindowPos(g_hwnd, nullptr, s_w.x + (p.x - s_m.x), s_w.y + (p.y - s_m.y), 0, 0, SWP_NOSIZE | SWP_NOZORDER);
    }
    if (ImGui::IsMouseReleased(ImGuiMouseButton_Left)) s_drag = false;

    const float btn_w = 40.0f, btn_h = 32.0f;
    float bx1 = wp.x + ws.x - 8.0f - btn_w;
    float bx0 = bx1 - btn_w;
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
    dl->AddRectFilled(p0, p1, bg_col, 0.0f);
    dl->AddRect(p0, p1, bd_col, 0.0f, 0, 1.5f);
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
                ColF(ImVec4(0, 0, 0, 1), 0.06f * a), 0.0f, 0, 1.0f);
        }
        ImU32 popup_bg = ColF(ImVec4(t.window_bg.x + 0.02f, t.window_bg.y + 0.02f, t.window_bg.z + 0.04f, 1.0f), 1.0f);
        ImU32 popup_bd = ColF(t.accent, 0.65f * a);
        fg->AddRectFilled(lp0, lp1, popup_bg);
        fg->AddRect(lp0, lp1, popup_bd, 0.0f, 0, 1.5f);

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
                fg->AddRectFilled(r0, r1, ColF(t.accent, (item_hover ? 0.45f : 0.30f) * a));
                fg->AddRectFilled(ImVec2(r0.x + 2, r0.y + 5), ImVec2(r0.x + 5, r1.y - 5), ColF(t.accent, 1.0f * a));
            }
            else if (item_hover) {
                fg->AddRectFilled(r0, r1, ColF(t.accent, 0.20f * a));
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
                ColF(t.accent, 0.65f * a));
        }
        fg->PopClipRect();
    }
    ImGui::PopID();
}

// ============================================================
//  Карточка (без скруглений)
// ============================================================
static void DrawGameCard(int gi, ImVec2 p0, ImVec2 p1, bool blocked,
    bool& out_copy, bool& out_hovered)
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
    out_hovered = hovered;
    g.copy_hover_t = ExpLerp(g.copy_hover_t, hovered ? 1.0f : 0.0f, 10.0f, dt);

    ImVec2 mp_now = ImGui::GetIO().MousePos;
    const char* lbl_steam = "Steam";
    ImVec2 ts_steam = ImGui::CalcTextSize(lbl_steam);
    float pad_x_s = 14.0f, pad_y_s = 7.0f;
    float bw_s = ts_steam.x + pad_x_s * 2, bh_s = ts_steam.y + pad_y_s * 2;
    ImVec2 sp1_s(p1.x - 10.0f, p1.y - 10.0f);
    ImVec2 sp0_s(sp1_s.x - bw_s, sp1_s.y - bh_s);
    bool over_steam = hovered && mp_now.x >= sp0_s.x && mp_now.x <= sp1_s.x && mp_now.y >= sp0_s.y && mp_now.y <= sp1_s.y;
    g.steam_hover_t = ExpLerp(g.steam_hover_t, (hovered || over_steam) ? 1.0f : 0.0f, 10.0f, dt);

    dl->AddRectFilled(p0, p1, ColF(hovered ? t.card_bg_hover : t.card_bg, 1.0f), 0.0f);
    if (g.texture) {
        if (g.open_time > 0.0f) {
            float t2 = MY_CLAMP(((float)ImGui::GetTime() - g.open_time) * 4.0f, 0.0f, 1.0f);
            g.fade_in = EaseOutCubic(t2);
        }
        dl->AddImage((ImTextureID)g.texture, p0, p1, ImVec2(0, 0), ImVec2(1, 1),
            ColF(ImVec4(1, 1, 1, 1), g.fade_in));
    }
    else {
        dl->AddRectFilled(p0, p1, IM_COL32(50, 48, 62, 255), 0.0f);
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
        dl->AddRectFilled(bp0, bp1, IM_COL32(80, 200, 120, 255), 0.0f);
        dl->AddText(ImVec2(bp0.x + px, bp0.y + py), IM_COL32(0, 0, 0, 255), lbl);
    }
    if (g.nsfw) {
        const char* lbl = "NSFW";
        ImVec2 ts = ImGui::CalcTextSize(lbl);
        float px = 6.0f, py = 3.0f;
        float bw = ts.x + px * 2, bh = ts.y + py * 2;
        ImVec2 bp0(p1.x - bw - 14, p0.y + 14);
        ImVec2 bp1(bp0.x + bw, bp0.y + bh);
        dl->AddRectFilled(bp0, bp1, IM_COL32(200, 60, 80, 200), 0.0f);
        dl->AddText(ImVec2(bp0.x + px, bp0.y + py), IM_COL32(255, 255, 255, 230), lbl);
    }

    ImVec2 mp = ImGui::GetIO().MousePos;

    const char* copy_lbl = "Copy ID";
    ImVec2 copy_ts = ImGui::CalcTextSize(copy_lbl);
    const float cp_pad_x = 10.0f;
    const float cp_pad_y = 6.0f;
    float cp_w = copy_ts.x + cp_pad_x * 2.0f;
    float cp_h = copy_ts.y + cp_pad_y * 2.0f;
    ImVec2 copy0(p0.x + 8.0f, p0.y + 8.0f);
    ImVec2 copy1(copy0.x + cp_w, copy0.y + cp_h);

    out_copy = false;

    if (g.copy_hover_t > 0.01f) {
        float a = g.copy_hover_t;
        bool ch = hovered && mp.x >= copy0.x && mp.x <= copy1.x && mp.y >= copy0.y && mp.y <= copy1.y;

        if (ch && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) out_copy = true;

        ImVec2 co(0, -(1.0f - a) * 8.0f);
        ImVec2 c0(copy0.x + co.x, copy0.y + co.y);
        ImVec2 c1(copy1.x + co.x, copy1.y + co.y);

        ImU32 bg = ch ? ColF(t.accent, 0.90f * a)
            : ColF(t.window_bg, 0.82f * a);
        dl->AddRectFilled(c0, c1, bg);

        ImU32 bd = ch ? IM_COL32(255, 255, 255, (int)(255 * a))
            : ColF(t.accent, 0.85f * a);
        dl->AddRect(c0, c1, bd, 0.0f, 0, 1.5f);

        ImU32 txt = ch ? IM_COL32(255, 255, 255, (int)(255 * a))
            : ColF(t.text, a);
        dl->AddText(ImVec2(c0.x + cp_pad_x, c0.y + cp_pad_y), txt, copy_lbl);
    }

    bool over_copy = hovered && mp.x >= copy0.x && mp.x <= copy1.x && mp.y >= copy0.y && mp.y <= copy1.y;
    bool show_tt = hovered && !blocked && !over_copy;
    if (show_tt) {
        ImDrawList* fg = ImGui::GetForegroundDrawList();
        ImVec2 ns = ImGui::CalcTextSize(g.name.c_str());
        char idb[64]; snprintf(idb, sizeof(idb), "AppID: %s", g.appid.c_str());
        ImVec2 ids = ImGui::CalcTextSize(idb);
        std::string tl = "Теги: ";
        if (g.tags.empty()) tl += "—";
        else for (size_t i = 0; i < g.tags.size(); ++i) { if (i) tl += ", "; tl += g.tags[i]; }
        if (g.nsfw) tl += "  [NSFW]";
        ImVec2 tls = ImGui::CalcTextSize(tl.c_str());
        float px = 12.0f, py = 10.0f;
        float bw = std::max({ ns.x, ids.x, tls.x }) + px * 2;
        float bh = ns.y + ids.y + tls.y + py * 2 + 10.0f;
        ImVec2 b0(mp.x + 16, mp.y + 16), b1(b0.x + bw, b0.y + bh);
        ImVec2 scr = ImGui::GetIO().DisplaySize;
        if (b1.x > scr.x - 10) b0.x = mp.x - bw - 16;
        if (b1.y > scr.y - 10) b0.y = mp.y - bh - 16;
        b1 = ImVec2(b0.x + bw, b0.y + bh);
        fg->AddRectFilled(b0, b1, IM_COL32(45, 45, 55, 250));
        fg->AddRect(b0, b1, ColF(t.accent, 0.7f), 0.0f, 0, 1.2f);
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

    bool input_blocked = IsInputBlocked();
    if (input_blocked) ImGui::BeginDisabled();

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
    float btn_w = 110.0f;
    ImGui::SameLine();
    ImGui::SetCursorPosX(content_right - combo_w - btn_w - 10.0f);

    // Кнопка "Обновить"
    {
        ImVec2 p0 = ImGui::GetCursorScreenPos();
        ImVec2 p1 = ImVec2(p0.x + btn_w, p0.y + combo_h);
        bool hov = ImGui::IsMouseHoveringRect(p0, p1);
        ImDrawList* dl2 = ImGui::GetWindowDrawList();

        ImU32 bg = ColF(t.accent, hov ? 0.35f : 0.15f);
        dl2->AddRectFilled(p0, p1, bg);
        dl2->AddRect(p0, p1, ColF(t.accent, hov ? 1.0f : 0.5f), 0.0f, 0, 1.5f);

        const char* lbl = "Обновить";
        ImVec2 ts = ImGui::CalcTextSize(lbl);
        dl2->AddText(ImVec2(p0.x + (btn_w - ts.x) * 0.5f, p0.y + (combo_h - ts.y) * 0.5f), ColF(t.text, 1.0f), lbl);

        if (hov && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
            int start = g_app.page * AppState::kGamesPerPage;
            int end = std::min((int)g_app.filtered.size(), start + AppState::kGamesPerPage);
            for (int i = start; i < end; i++) {
                GameEntry& ge = g_app.all_games[g_app.filtered[i]];
                if (ge.texture) { ge.texture->Release(); ge.texture = nullptr; }
                ge.loading = false;
                ge.load_failed = false;
                ge.open_time = -1.0f;
                ge.fade_in = 0.0f;
            }
            LogPush("Обновление картинок страницы " + std::to_string(g_app.page + 1));
        }
    }

    ImGui::SameLine();
    ImGui::SetCursorPosX(content_right - combo_w);
    DrawTagComboBox(combo_w, combo_h);

    ImGui::Spacing(); ImGui::Spacing();

    if (g_app.all_games.empty()) { ImGui::Dummy(ImVec2(0, 40)); ImGui::TextColored(t.text_disabled, "Загрузка базы игр..."); if (input_blocked) ImGui::EndDisabled(); return; }
    if (g_app.filtered.empty()) { ImGui::Dummy(ImVec2(0, 40)); ImGui::TextColored(t.text_disabled, "По выбранному фильтру ничего не найдено."); if (input_blocked) ImGui::EndDisabled(); return; }

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
        bool c_c = false;
        bool card_hovered = false;
        DrawGameCard(gi, p0, p1, block, c_c, card_hovered);

        ImGui::SetCursorScreenPos(p0);
        ImGui::PushID(gi);
        ImGui::InvisibleButton("rand_card", ImVec2(card_w, card_h));
        bool card_clicked = ImGui::IsItemClicked();
        ImGui::PopID();

        bool st_c = false;
        if (!block && g_app.all_games[gi].steam_hover_t > 0.01f) {
            float steam_a = g_app.all_games[gi].steam_hover_t;

            ImVec2 mp = ImGui::GetIO().MousePos;
            const char* lbl = "Steam";
            ImVec2 ts = ImGui::CalcTextSize(lbl);
            float pad_x = 14.0f, pad_y = 7.0f;
            float bw = ts.x + pad_x * 2, bh = ts.y + pad_y * 2;
            ImVec2 sp1(p1.x - 10.0f, p1.y - 10.0f);
            ImVec2 sp0(sp1.x - bw, sp1.y - bh);
            bool sh = mp.x >= sp0.x && mp.x <= sp1.x && mp.y >= sp0.y && mp.y <= sp1.y;

            ImU32 bg = sh ? IM_COL32(30, 130, 220, (int)(255 * steam_a))
                : IM_COL32(20, 90, 160, (int)(235 * steam_a));
            ImU32 bd = sh ? IM_COL32(120, 200, 255, (int)(255 * steam_a))
                : IM_COL32(80, 160, 230, (int)(200 * steam_a));

            dl->AddRectFilled(sp0, sp1, bg);
            dl->AddRect(sp0, sp1, bd, 0.0f, 0, 1.5f);
            dl->AddText(ImVec2(sp0.x + pad_x, sp0.y + pad_y), IM_COL32(255, 255, 255, (int)(255 * steam_a)), lbl);

            if (sh && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) st_c = true;
        }

        if (st_c) OpenGameInSteam(g.appid);
        else if (c_c) { CopyToClipboard(g.appid); LogPush("AppID скопирован: " + g.appid); }
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

            bool c_c = false;
            bool card_hovered = false;
            DrawGameCard(gi, p0, p1, block, c_c, card_hovered);

            ImGui::PushID(gi);
            ImGui::SetCursorScreenPos(p0);
            ImGui::InvisibleButton("card", ImVec2(card_w, card_h));
            bool card_clicked = ImGui::IsItemClicked();
            ImGui::PopID();

            bool st_c = false;
            if (!block && g_app.all_games[gi].steam_hover_t > 0.01f) {
                float steam_a = g_app.all_games[gi].steam_hover_t;

                ImVec2 mp = ImGui::GetIO().MousePos;
                const char* lbl = "Steam";
                ImVec2 ts = ImGui::CalcTextSize(lbl);
                float pad_x = 14.0f, pad_y = 7.0f;
                float bw = ts.x + pad_x * 2, bh = ts.y + pad_y * 2;
                ImVec2 sp1(p1.x - 10.0f, p1.y - 10.0f);
                ImVec2 sp0(sp1.x - bw, sp1.y - bh);
                bool sh = mp.x >= sp0.x && mp.x <= sp1.x && mp.y >= sp0.y && mp.y <= sp1.y;

                ImU32 bg = sh ? IM_COL32(30, 130, 220, (int)(255 * steam_a))
                    : IM_COL32(20, 90, 160, (int)(235 * steam_a));
                ImU32 bd = sh ? IM_COL32(120, 200, 255, (int)(255 * steam_a))
                    : IM_COL32(80, 160, 230, (int)(200 * steam_a));

                dl->AddRectFilled(sp0, sp1, bg);
                dl->AddRect(sp0, sp1, bd, 0.0f, 0, 1.5f);
                dl->AddText(ImVec2(sp0.x + pad_x, sp0.y + pad_y), IM_COL32(255, 255, 255, (int)(255 * steam_a)), lbl);

                if (sh && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) st_c = true;
            }

            if (st_c) OpenGameInSteam(g_app.all_games[gi].appid);
            else if (c_c) {
                CopyToClipboard(g_app.all_games[gi].appid);
                LogPush("AppID скопирован: " + g_app.all_games[gi].appid);
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
                    ColF(t.accent, g_app.outline_alpha * (0.06f / i)), 0.0f, 0, 2.0f);
            }
            dl->AddRect(o0, o1, ColF(t.accent, g_app.outline_alpha), 0.0f, 0, 2.5f);
        }
    }

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

    {
        const char* lbl = "Случайная игра";
        ImVec2 ts = ImGui::CalcTextSize(lbl);
        float bw = ts.x + 40.0f, bh = 40.0f;
        float bx = grid_origin.x + pg_sz.x + 30.0f;
        float byy = pager_y + (pager_h - bh) * 0.5f;
        ImVec2 b0(bx, byy), b1(bx + bw, byy + bh);
        bool hv = mp.x >= b0.x && mp.x <= b1.x && mp.y >= b0.y && mp.y <= b1.y;
        ImU32 bg = hv ? ColF(t.accent, 0.35f) : ColF(t.accent, 0.15f);
        pdl->AddRectFilled(b0, b1, bg);
        pdl->AddRect(b0, b1, ColF(t.accent, hv ? 1.0f : 0.5f), 0.0f, 0, 1.5f);
        pdl->AddText(ImVec2(b0.x + 20, b0.y + (bh - ts.y) * 0.5f), ColF(t.text, 1.0f), lbl);
        if (hv && ImGui::IsMouseClicked(ImGuiMouseButton_Left) && !busy && !block && !g_app.filtered.empty()) {
            std::mt19937 rng((unsigned)std::chrono::system_clock::now().time_since_epoch().count());
            std::uniform_int_distribution<int> dist(0, (int)g_app.filtered.size() - 1);
            g_app.random_index = g_app.filtered[dist(rng)];
            g_app.random_mode = true;
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
                        pdl->AddRect(ImVec2(s0.x - e, s0.y - e), ImVec2(s1.x + e, s1.y + e), ColF(t.accent, 0.10f / i), 0.0f, 0, 1.5f);
                    }
                    pdl->AddRectFilled(s0, s1, IM_COL32(255, 255, 255, 15));
                    pdl->AddRect(s0, s1, ColF(t.accent, 1.0f), 0.0f, 0, 2.5f);
                    snprintf(b, sizeof(b), "%d", pn);
                    ImVec2 ts = ImGui::CalcTextSize(b);
                    pdl->AddText(ImVec2(s0.x + (sq - ts.x) * 0.5f, s0.y + (sq - ts.y) * 0.5f), ColF(t.text, 1.0f), b);
                }
                else {
                    snprintf(b, sizeof(b), "%d", pn);
                    ImVec2 ts = ImGui::CalcTextSize(b);
                    ImU32 bd = hv ? ColF(t.accent, 0.8f) : IM_COL32(180, 180, 190, 200);
                    pdl->AddRect(s0, s1, bd, 0.0f, 0, 1.8f);
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
            ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 0.0f);
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
        pdl->AddRectFilled(b0, b1, bg);
        pdl->AddRect(b0, b1, ColF(t.accent2, hv ? 1.0f : 0.5f), 0.0f, 0, 1.5f);
        pdl->AddText(ImVec2(b0.x + 15, b0.y + (bh - ts.y) * 0.5f), ColF(t.text, 1.0f), lbl);
        if (hv && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) { g_app.random_mode = false; g_app.random_index = -1; }
    }

    if (input_blocked) ImGui::EndDisabled();
}

// ============================================================
//  БИБЛИОТЕКА
// ============================================================
static void DrawLibraryPage() {
    ThemeColors t = GetTheme();
    bool busy = g_app.IsBusy();
    if (busy) ImGui::BeginDisabled();

    ImGui::Dummy(ImVec2(0, 4));
    ImGui::Indent(24.0f);
    if (ImGui::Button("Обновить список", ImVec2(180, 34))) { RefreshInstalledFiles(); RefreshInstalledFlags(); LogPush("Список библиотеки обновлён"); }
    ImGui::SameLine(0, 12);
    if (ImGui::Button("Открыть папку Lua", ImVec2(180, 34))) OpenLuaFolder();
    ImGui::Unindent(24.0f);
    ImGui::Spacing(); ImGui::Separator(); ImGui::Spacing();

    if (g_app.installed.empty()) {
        ImGui::Indent(24.0f);
        ImGui::TextDisabled("В библиотеке пока нет установленных игр.");
        ImGui::Unindent(24.0f);
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
        SaveSettings();
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
        SaveSettings();
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

    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
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
            dl->AddRect(pv0, pv1, ColF(t.accent, 0.5f), 0.0f, 0, 1.5f);
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
            SaveSettings();
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
    if (ImGui::Checkbox("Снег", &s1)) { g_fx_snow = s1; SaveSettings(); }
    ImGui::SameLine();
    if (ImGui::Checkbox("Дождь", &s2)) { g_fx_rain = s2; SaveSettings(); }
    ImGui::SameLine();
    if (ImGui::Checkbox("Листья", &s3)) { g_fx_leaves = s3; SaveSettings(); }

    ImGui::Unindent(24.0f);
    if (busy) ImGui::EndDisabled();
}

// ============================================================
//  ЛОГИ
// ============================================================
static void DrawLogsPage() {
    ThemeColors t = GetTheme();
    ImGui::Dummy(ImVec2(0, 4));
    ImGui::Indent(24.0f);

    if (ImGui::Button("Очистить логи", ImVec2(180, 34))) {
        std::lock_guard<std::mutex> lk(g_app.log_mtx);
        g_app.log.clear();
        g_app.notifications_count = 0;
    }
    ImGui::Unindent(24.0f);
    ImGui::Spacing(); ImGui::Separator(); ImGui::Spacing();

    ImGui::Indent(24.0f);
    ImGui::BeginChild("##log_scroll", ImVec2(0, 0), false);
    std::lock_guard<std::mutex> lk(g_app.log_mtx);
    for (auto& e : g_app.log) {
        std::time_t tt = std::chrono::system_clock::to_time_t(e.time);
        std::tm tm; localtime_s(&tm, &tt);
        char time_buf[32];
        snprintf(time_buf, sizeof(time_buf), "%02d:%02d:%02d", tm.tm_hour, tm.tm_min, tm.tm_sec);

        ImGui::PushStyleColor(ImGuiCol_Text, e.is_error ? ImVec4(1.0f, 0.4f, 0.4f, 1.0f) : t.text_disabled);
        ImGui::Text("[%s]", time_buf);
        ImGui::PopStyleColor();
        ImGui::SameLine();
        ImGui::PushStyleColor(ImGuiCol_Text, e.is_error ? ImVec4(1.0f, 0.5f, 0.5f, 1.0f) : t.text);
        ImGui::TextWrapped("%s", e.text.c_str());
        ImGui::PopStyleColor();
        ImGui::Separator();
    }
    ImGui::EndChild();
    ImGui::Unindent(24.0f);
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
        fg->AddRectFilled(ImVec2(wx, wy), ImVec2(wx + ww, wy + wh), IM_COL32(20, 16, 32, 255));
        fg->AddRect(ImVec2(wx, wy), ImVec2(wx + ww, wy + wh), ColF(t.accent, 0.6f), 0.0f, 0, 1.5f);
        const char* title = "Загрузка манифеста";
        const char* sub = "удалит скачанное этой игры";
        ImU32 bar = ColF(t.accent, 1.0f);
        if (mode == ProgressMode::Delete) { title = "Удаление файлов"; sub = "идёт удаление..."; bar = IM_COL32(217, 89, 110, 255); }
        else if (mode == ProgressMode::InstallTools) { title = "Установка OpenSteamTools"; sub = "распаковка..."; bar = IM_COL32(64, 179, 115, 255); }
        fg->AddText(ImVec2(wx + 24, wy + 20), ColF(t.accent, 1.0f), title);
        { std::lock_guard<std::mutex> lk(g_app.status_mtx); fg->AddText(ImVec2(wx + 24, wy + 52), IM_COL32(230, 230, 240, 255), g_app.current_name.c_str()); }
        const float bx = wx + 24, by = wy + 92, bw = ww - 48, bh = 24;
        fg->AddRectFilled(ImVec2(bx, by), ImVec2(bx + bw, by + bh), IM_COL32(255, 255, 255, 20));
        float prog = MY_CLAMP(g_app.progress_display, 0.0f, 1.0f);
        fg->AddRectFilled(ImVec2(bx, by), ImVec2(bx + bw * prog, by + bh), bar);
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
            fg->AddRectFilled(ImVec2(bbx, bby), ImVec2(bbx + bbw, bby + bbh), bc);
            ImVec2 cts = ImGui::CalcTextSize("Отмена");
            fg->AddText(ImVec2(bbx + (bbw - cts.x) * 0.5f, bby + (bbh - cts.y) * 0.5f), IM_COL32(255, 255, 255, 255), "Отмена");
            if (hv && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) CancelCurrentDownload();
        }
    }

    if (g_app.success_visible) {
        g_app.success_timer += dt;
        const float t_in = 0.18f, t_hold = 0.90f, t_out = 0.45f;
        float total = t_in + t_hold + t_out;
        if (g_app.success_timer < t_in) {
            float p = g_app.success_timer / t_in;
            g_app.success_alpha = 1.0f - (1.0f - p) * (1.0f - p);
        }
        else if (g_app.success_timer < t_in + t_hold) {
            g_app.success_alpha = 1.0f;
        }
        else if (g_app.success_timer < total) {
            float p = (g_app.success_timer - t_in - t_hold) / t_out;
            g_app.success_alpha = 1.0f - p;
        }
        else {
            g_app.success_alpha = 0.0f;
            g_app.success_visible = false;
            g_app.success_timer = 0.0f;
        }
        if (g_app.success_visible && g_app.success_alpha > 0.001f) {
            float cx = io.DisplaySize.x * 0.5f, cy = io.DisplaySize.y * 0.5f;
            float a = g_app.success_alpha;

            float draw_p = g_app.success_timer / 0.35f;
            if (draw_p < 0.0f) draw_p = 0.0f; if (draw_p > 1.0f) draw_p = 1.0f;
            float dp = 1.0f - draw_p; dp = 1.0f - dp * dp * dp;

            for (int i = 4; i >= 1; i--) {
                float r = 72.0f + i * 8.0f;
                fg->AddCircleFilled(ImVec2(cx, cy), r, ColF(t.accent, 0.08f * a * (5 - i) / 4.0f), 64);
            }
            fg->AddCircleFilled(ImVec2(cx, cy), 84.0f, ColF(t.accent, 0.28f * a), 64);
            float ring_r = 72.0f;
            for (int k = 0; k < 5; k++) {
                float rr = ring_r - k * 1.6f;
                fg->AddCircle(ImVec2(cx, cy), rr, ColF(t.accent, a), 64, 2.0f);
            }
            fg->AddCircleFilled(ImVec2(cx, cy), ring_r - 8.0f, ColF(t.accent, 0.10f * a), 64);
            fg->PathClear();
            fg->PathArcTo(ImVec2(cx, cy), ring_r - 4.0f,
                3.1415926f * 1.15f, 3.1415926f * 1.85f, 32);
            fg->PathStroke(IM_COL32(255, 255, 255, (int)(90 * a)), 0, 1.8f);

            ImVec2 A(cx - 30.0f, cy + 2.0f);
            ImVec2 B(cx - 8.0f, cy + 26.0f);
            ImVec2 C(cx + 34.0f, cy - 24.0f);
            float th = 9.0f;
            ImU32 chk_col = IM_COL32(255, 255, 255, (int)(255 * a));

            if (dp > 0.0f) {
                float p1 = MY_CLAMP(dp / 0.55f, 0.0f, 1.0f);
                ImVec2 B1(B.x * p1 + A.x * (1 - p1), B.y * p1 + A.y * (1 - p1));
                fg->AddLine(A, B1, chk_col, th);
                fg->AddCircleFilled(B1, th * 0.5f, chk_col, 12);
                if (dp > 0.55f) {
                    float p2 = MY_CLAMP((dp - 0.55f) / 0.45f, 0.0f, 1.0f);
                    ImVec2 C1(C.x * p2 + B.x * (1 - p2), C.y * p2 + B.y * (1 - p2));
                    fg->AddLine(B, C1, chk_col, th);
                    if (p2 < 0.99f) fg->AddCircleFilled(B, th * 0.5f, chk_col, 12);
                }
            }

            for (int i = 0; i < 8; i++) {
                float ang = i * (2.0f * 3.1415926f / 8.0f) + g_app.success_timer * 2.0f;
                float rad = ring_r + 8.0f + std::sin(g_app.success_timer * 6.0f + i) * 3.0f;
                float px = cx + cosf(ang) * rad;
                float py = cy + sinf(ang) * rad;
                float sz = 2.0f + std::sin(g_app.success_timer * 4.0f + i * 1.3f) * 1.0f;
                fg->AddCircleFilled(ImVec2(px, py), sz, ColF(t.accent, 0.55f * a), 8);
            }
        }
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

    ImVec2 scr = io.DisplaySize;
    float cx = scr.x * 0.5f, cy = scr.y * 0.5f;
    float time = (float)ImGui::GetTime();

    fg->AddRectFilled(ImVec2(0, 0), scr,
        IM_COL32((int)(t.window_bg.x * 255), (int)(t.window_bg.y * 255), (int)(t.window_bg.z * 255), (int)(255 * a)));

    for (int i = 8; i >= 1; i--) {
        float r = 140.0f + i * 40.0f;
        float al = 0.035f * a * (9 - i) / 8.0f;
        fg->AddCircleFilled(ImVec2(cx, cy - 40.0f), r, ColF(t.accent, al), 64);
    }

    float logo_size = 220.0f;
    float logo_y = cy - logo_size * 0.5f - 40.0f;
    float logo_x = cx - logo_size * 0.5f;

    if (g_logo_srv) {
        float pulse = 0.85f + 0.15f * std::sin(time * 1.6f);
        for (int i = 3; i >= 1; i--) {
            float e = (float)i * 3.0f;
            float al = 0.04f * a * pulse / i;
            fg->AddRectFilled(
                ImVec2(logo_x - e, logo_y - e),
                ImVec2(logo_x + logo_size + e, logo_y + logo_size + e),
                ColF(t.accent, al), 12.0f);
        }
        fg->AddImage((ImTextureID)g_logo_srv,
            ImVec2(logo_x, logo_y),
            ImVec2(logo_x + logo_size, logo_y + logo_size),
            ImVec2(0, 0), ImVec2(1, 1),
            IM_COL32(255, 255, 255, (int)(255 * a)));
    }

    const char* sign = "Developer by : RodekPlay";
    float sign_y = logo_y + logo_size + 40.0f;

    if (g_font_huge) ImGui::PushFont(g_font_huge);
    ImVec2 ssz = ImGui::CalcTextSize(sign);
    float sxx = cx - ssz.x * 0.5f;
    {
        float line_y = sign_y + ssz.y * 0.5f;
        float gap = 26.0f;
        float len = 110.0f;

        ImVec2 l0(cx - ssz.x * 0.5f - gap - len, line_y);
        ImVec2 l1(cx - ssz.x * 0.5f - gap, line_y);
        fg->AddLine(l0, l1, ColF(t.accent, 0.55f * a), 1.5f);
        fg->AddCircleFilled(ImVec2(l1.x, l1.y), 2.8f, ColF(t.accent, 0.9f * a), 8);

        ImVec2 r0(cx + ssz.x * 0.5f + gap, line_y);
        ImVec2 r1(cx + ssz.x * 0.5f + gap + len, line_y);
        fg->AddLine(r0, r1, ColF(t.accent, 0.55f * a), 1.5f);
        fg->AddCircleFilled(ImVec2(r0.x, r0.y), 2.8f, ColF(t.accent, 0.9f * a), 8);
    }
    fg->AddText(ImVec2(sxx, sign_y), ColF(t.accent, a), sign);
    if (g_font_huge) ImGui::PopFont();

    const char* status = g_splash.step_text.c_str();
    ImVec2 sts = ImGui::CalcTextSize(status);
    float status_y = sign_y + ssz.y + 22.0f;
    fg->AddText(ImVec2(cx - sts.x * 0.5f, status_y), ColF(t.text_disabled, a), status);

    {
        float bar_w = 320.0f;
        float bar_h = 3.0f;
        float bar_x = cx - bar_w * 0.5f;
        float bar_y = status_y + sts.y + 18.0f;

        fg->AddRectFilled(ImVec2(bar_x, bar_y), ImVec2(bar_x + bar_w, bar_y + bar_h),
            ColF(t.text, 0.10f * a));

        float seg_w = 90.0f;
        float total = bar_w + seg_w;
        float pos = fmodf(time * 180.0f, total) - seg_w;
        float x0 = bar_x + (pos < 0 ? 0 : pos);
        float x1 = bar_x + (pos + seg_w > bar_w ? bar_w : pos + seg_w);

        if (x1 > x0) {
            fg->AddRectFilled(ImVec2(x0, bar_y), ImVec2(x1, bar_y + bar_h),
                ColF(t.accent, 0.95f * a));
            for (int i = 3; i >= 1; i--) {
                float e = (float)i * 2.0f;
                fg->AddRectFilled(
                    ImVec2(x0 - e, bar_y - e),
                    ImVec2(x1 + e, bar_y + bar_h + e),
                    ColF(t.accent, 0.08f * a / i));
            }
        }
    }
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

    DrawSidebar();
    ImGui::SameLine(0, 0);

    ImGui::BeginChild("##main_area", ImVec2(0, 0), false, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
    ImGui::BeginChild("##main_area_pad", ImVec2(0, 0), false, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);

    DrawMainTopBar();

    ImGui::BeginChild("##content", ImVec2(0, 0), false, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
    ImGui::BeginChild("##content_pad", ImVec2(0, 0), false, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);

    switch (g_app.current_tab) {
    case 0: DrawShopPage(); break;
    case 1: DrawLibraryPage(); break;
    case 2: DrawToolsPage(); break;
    case 3: DrawLogsPage(); break;
    default: break;
    }

    DrawCustomGradientPopup();

    ImGui::EndChild();
    ImGui::PopStyleVar();
    ImGui::EndChild();

    ImGui::EndChild();
    ImGui::PopStyleVar();
    ImGui::EndChild();

    ImGui::PopStyleVar();
    ImGui::End();

    if (IsInputBlocked()) {
        ImGui::SetNextWindowPos(vp->WorkPos);
        ImGui::SetNextWindowSize(vp->WorkSize);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
        ImGui::PushStyleColor(ImGuiCol_WindowBg, IM_COL32(0, 0, 0, 0));
        ImGui::Begin("##click_blocker", nullptr,
            ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
            ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse |
            ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoBringToFrontOnFocus |
            ImGuiWindowFlags_NoNavFocus);
        ImGui::End();
        ImGui::PopStyleColor();
        ImGui::PopStyleVar();
    }

    DrawOverlayAndProgress();
    ProcessPendingTextures();

    if (g_splash.active) DrawSplashScreen();
}

// ============================================================
//  Init
// ============================================================
static std::atomic<bool> g_init_done{ false };

static void BackgroundInitThread() {
    try {
        g_splash.step_text = "Загрузка настроек...";
        LogPush("=== SteamLuaTools 10.2 запуск ===");
        LogPush("Путь настроек: " + GetSettingsPath());
        LoadSettings();
        ApplyStyle();
        LogPush("Настройки загружены. Тема: " + std::to_string((int)g_theme) + ", FX: " + std::to_string((int)g_fx));

        g_splash.step_text = "Поиск Steam...";
        LogPush("Поиск Steam...");
        g_app_steam_path = FindSteamPath();
        g_app.steam_found = !g_app_steam_path.empty();
        if (g_app.steam_found) {
            LogPush("Steam найден: " + g_app_steam_path);
            LogPush("Папка Lua: " + (fs::path(g_app_steam_path) / "config" / "lua").string());
            LogPush("Папка Depotcache: " + (fs::path(g_app_steam_path) / "depotcache").string());
        }
        else LogPush("Steam не найден в системе", true);

        g_splash.step_text = "Загрузка базы игр...";
        LogPush("Загрузка базы игр gamespic.json...");
        if (LoadGamesJson()) {
            LogPush("Загружено игр: " + std::to_string(g_app.all_games.size()));
            LogPush("Доступно тегов: " + std::to_string((int)g_app.available_tags.size()));
        }
        else {
            LogPush("Не удалось загрузить базу игр gamespic.json", true);
        }

        LoadSettings();
        RefreshInstalledFlags();
        RefreshInstalledFiles();
        LogPush("Установленных игр (lua-файлов): " + std::to_string(g_app.installed.size()));

        RebuildFiltered();
        LogPush("Отфильтровано записей: " + std::to_string(g_app.filtered.size()));

        g_splash.step_text = "Готово";
        LogPush("=== Инициализация завершена ===");
        g_init_done = true;
    }
    catch (...) {
        LogPush("INIT CRASH", true);
        g_init_done = true;
    }
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
static bool LoadSmallIconTexture() {
    HINSTANCE hInst = GetModuleHandle(nullptr);
    HICON hIcon = (HICON)LoadImageW(hInst, MAKEINTRESOURCEW(IDI_ICON1), IMAGE_ICON, 64, 64, LR_DEFAULTCOLOR);
    if (!hIcon) return false;

    ICONINFO ii = {};
    if (!GetIconInfo(hIcon, &ii)) { DestroyIcon(hIcon); return false; }

    BITMAP bm = {};
    GetObject(ii.hbmColor, sizeof(bm), &bm);
    int w = bm.bmWidth, h = bm.bmHeight;
    if (w <= 0 || h <= 0) {
        if (ii.hbmColor) DeleteObject(ii.hbmColor);
        if (ii.hbmMask) DeleteObject(ii.hbmMask);
        DestroyIcon(hIcon);
        return false;
    }

    BITMAPINFO bi = {};
    bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bi.bmiHeader.biWidth = w;
    bi.bmiHeader.biHeight = -h;
    bi.bmiHeader.biPlanes = 1;
    bi.bmiHeader.biBitCount = 32;
    bi.bmiHeader.biCompression = BI_RGB;

    std::vector<unsigned char> pixels((size_t)w * h * 4, 0);
    HDC hdc = GetDC(nullptr);
    int ok = GetDIBits(hdc, ii.hbmColor, 0, h, pixels.data(), &bi, DIB_RGB_COLORS);
    ReleaseDC(nullptr, hdc);

    if (ii.hbmColor) DeleteObject(ii.hbmColor);
    if (ii.hbmMask) DeleteObject(ii.hbmMask);
    DestroyIcon(hIcon);

    if (!ok) return false;

    for (int i = 0; i < w * h; i++) {
        unsigned char tmp = pixels[i * 4 + 0];
        pixels[i * 4 + 0] = pixels[i * 4 + 2];
        pixels[i * 4 + 2] = tmp;
    }

    D3D11_TEXTURE2D_DESC desc = {};
    desc.Width = w; desc.Height = h; desc.MipLevels = 1; desc.ArraySize = 1;
    desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    desc.SampleDesc.Count = 1;
    desc.Usage = D3D11_USAGE_DEFAULT;
    desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
    D3D11_SUBRESOURCE_DATA sub = {};
    sub.pSysMem = pixels.data();
    sub.SysMemPitch = w * 4;

    ID3D11Texture2D* tex = nullptr;
    if (FAILED(g_pd3dDevice->CreateTexture2D(&desc, &sub, &tex))) return false;
    HRESULT hr = g_pd3dDevice->CreateShaderResourceView(tex, nullptr, &g_icon_small_srv);
    tex->Release();
    if (FAILED(hr)) return false;

    g_icon_small_w = w;
    g_icon_small_h = h;
    return true;
}
static void ApplySoftEdge(std::vector<unsigned char>& px, int w, int h, float soft_frac) {
    int soft_x = (int)(w * soft_frac);
    int soft_y = (int)(h * soft_frac);
    if (soft_x < 1) soft_x = 1;
    if (soft_y < 1) soft_y = 1;

    for (int y = 0; y < h; y++) {
        float fy = 1.0f;
        if (y < soft_y)          fy = (float)y / soft_y;
        else if (y >= h - soft_y) fy = (float)(h - 1 - y) / soft_y;
        if (fy < 0) fy = 0; if (fy > 1) fy = 1;

        for (int x = 0; x < w; x++) {
            float fx = 1.0f;
            if (x < soft_x)          fx = (float)x / soft_x;
            else if (x >= w - soft_x) fx = (float)(w - 1 - x) / soft_x;
            if (fx < 0) fx = 0; if (fx > 1) fx = 1;

            float fade = fx * fy;
            unsigned char* p = &px[(y * w + x) * 4 + 3];
            *p = (unsigned char)(*p * fade);
        }
    }
}

static bool LoadLogoTexture() {
    const char* candidates[] = { "logo.png", "./logo.png", "../logo.png" };
    std::string path;
    for (auto p : candidates) if (fs::exists(p)) { path = p; break; }
    if (path.empty()) return false;

    std::ifstream f(path, std::ios::binary);
    if (!f) return false;
    std::vector<unsigned char> raw((std::istreambuf_iterator<char>(f)),
        std::istreambuf_iterator<char>());
    if (raw.empty()) return false;

    int w, h, comp;
    unsigned char* pixels = stbi_load_from_memory(raw.data(), (int)raw.size(), &w, &h, &comp, 4);
    if (!pixels) return false;

    std::vector<unsigned char> px(pixels, pixels + (size_t)w * h * 4);
    stbi_image_free(pixels);

    ApplySoftEdge(px, w, h, 0.06f);

    D3D11_TEXTURE2D_DESC desc = {};
    desc.Width = w; desc.Height = h; desc.MipLevels = 1; desc.ArraySize = 1;
    desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    desc.SampleDesc.Count = 1;
    desc.Usage = D3D11_USAGE_DEFAULT;
    desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
    D3D11_SUBRESOURCE_DATA sub = {};
    sub.pSysMem = px.data();
    sub.SysMemPitch = w * 4;

    ID3D11Texture2D* tex = nullptr;
    if (FAILED(g_pd3dDevice->CreateTexture2D(&desc, &sub, &tex))) return false;
    HRESULT hr = g_pd3dDevice->CreateShaderResourceView(tex, nullptr, &g_logo_srv);
    tex->Release();
    if (FAILED(hr)) return false;

    g_logo_w = w;
    g_logo_h = h;
    return true;
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

    HWND hwnd = ::CreateWindowExW(
        WS_EX_LAYERED,
        wc.lpszClassName, L"SteamLuaTools 10.2",
        WS_POPUP | WS_MINIMIZEBOX, px, py, win_w, win_h,
        nullptr, nullptr, wc.hInstance, nullptr);
    g_hwnd = hwnd;

    ApplyAppIcon(hwnd);
    BOOL dark = TRUE;
    DwmSetWindowAttribute(hwnd, 20, &dark, sizeof(dark));

    ::ShowWindow(hwnd, SW_SHOW);
    ::UpdateWindow(hwnd);
    SetLayeredWindowAttributes(hwnd, 0, 0, LWA_ALPHA);

    if (!CreateDeviceD3D(hwnd)) {
        if (g_icon_small_srv) { g_icon_small_srv->Release(); g_icon_small_srv = nullptr; }
        if (g_logo_srv) { g_logo_srv->Release(); g_logo_srv = nullptr; }
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
    LoadSmallIconTexture();
    LoadLogoTexture();

    g_splash.active = true;
    g_splash.ready = false;
    g_splash.timer = 0.0f;
    g_splash.alpha = 1.0f;
    g_splash.step_text = "Инициализация...";
    g_init_done = false;

    std::thread(BackgroundInitThread).detach();

    float window_fade = 0.0f;
    const float fade_duration = 0.55f;
    bool  layered_done = false;

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

        if (!layered_done) {
            window_fade += ImGui::GetIO().DeltaTime / fade_duration;
            if (window_fade >= 1.0f) {
                window_fade = 1.0f;
                layered_done = true;
                LONG_PTR ex = GetWindowLongPtrW(hwnd, GWL_EXSTYLE);
                SetWindowLongPtrW(hwnd, GWL_EXSTYLE, ex & ~WS_EX_LAYERED);
            }
            else {
                float p = 1.0f - window_fade;
                float eased = 1.0f - p * p * p;
                BYTE a = (BYTE)(eased * 255.0f);
                SetLayeredWindowAttributes(hwnd, 0, a, LWA_ALPHA);
            }
        }

        g_pSwapChain->Present(1, 0);
    }

    ImGui_ImplDX11_Shutdown();
    ImGui_ImplWin32_Shutdown();
    ImGui::DestroyContext();
    for (auto& g : g_app.all_games) if (g.texture) g.texture->Release();
    if (g_icon_small_srv) { g_icon_small_srv->Release(); g_icon_small_srv = nullptr; }
    if (g_logo_srv) { g_logo_srv->Release(); g_logo_srv = nullptr; }
    CleanupDeviceD3D();
    ::DestroyWindow(hwnd);
    ::UnregisterClassW(wc.lpszClassName, wc.hInstance);
    return 0;
}

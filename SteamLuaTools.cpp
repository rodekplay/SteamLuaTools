// ============================================================
//  SteamLuaTools 9.1 — UI (SteamGameBrowser style) + механика
//  C++ + Dear ImGui + Win32 + DirectX 11 + WinHTTP + stb_image + nlohmann/json
// ============================================================
#pragma execution_character_set("utf-8")

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
#include <thread>
#include <mutex>
#include <atomic>
#include <chrono>
#include <cmath>
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
//  DirectX 11
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
static LRESULT WINAPI WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

static HWND g_hwnd = nullptr;

// Steam path (объявлен здесь — используется в механике выше остального)
static std::string g_app_steam_path;

// ============================================================
//  Утилиты анимации
// ============================================================
static float ExpLerp(float current, float target, float speed, float dt) {
    return current + (target - current) * (1.0f - std::exp(-speed * dt));
}
static ImVec2 ExpLerp2(ImVec2 cur, ImVec2 tgt, float speed, float dt) {
    return ImVec2(ExpLerp(cur.x, tgt.x, speed, dt), ExpLerp(cur.y, tgt.y, speed, dt));
}
static float EaseOutCubic(float t) { float p = 1.0f - t; return 1.0f - p * p * p; }

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
    cfg.OversampleH = 3;
    cfg.OversampleV = 3;
    cfg.PixelSnapH = true;

    for (auto path : candidates) {
        if (fs::exists(path)) {
            g_font_regular = io.Fonts->AddFontFromFileTTF(path, 16.0f, &cfg, io.Fonts->GetGlyphRangesCyrillic());
            g_font_small = io.Fonts->AddFontFromFileTTF(path, 13.0f, &cfg, io.Fonts->GetGlyphRangesCyrillic());
            g_font_big = io.Fonts->AddFontFromFileTTF(path, 24.0f, &cfg, io.Fonts->GetGlyphRangesCyrillic());
            g_font_title = io.Fonts->AddFontFromFileTTF(path, 21.0f, &cfg, io.Fonts->GetGlyphRangesCyrillic());
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
    if (hBig)   SendMessageW(hwnd, WM_SETICON, ICON_BIG, (LPARAM)hBig);
    if (hSmall) SendMessageW(hwnd, WM_SETICON, ICON_SMALL, (LPARAM)hSmall);
}

// ============================================================
//  Настройки
// ============================================================
static std::string GetSettingsPath() {
    char* appdata = nullptr;
    size_t len = 0;
    _dupenv_s(&appdata, &len, "APPDATA");
    std::string base;
    if (appdata) { base = appdata; free(appdata); }
    else base = ".";
    fs::path dir = fs::path(base) / "SteamLuaTools";
    std::error_code ec;
    fs::create_directories(dir, ec);
    return (dir / "settings.json").string();
}

// ============================================================
//  ТЕМА
// ============================================================
enum class ThemeMode {
    Dark = 0, Blue, Purple, Green, Red, Orange, Pink, Teal,
    Yellow, Gray, Sunset, Ocean, Forest
};
static ThemeMode g_theme = ThemeMode::Dark;

struct ThemeColors {
    ImVec4 window_bg;
    ImVec4 topbar_bg;
    ImVec4 accent;
    ImVec4 accent2;
    ImVec4 accent3;
    ImVec4 accent_hover;
    ImVec4 accent_active;
    ImVec4 card_bg;
    ImVec4 card_bg_hover;
    ImVec4 text;
    ImVec4 text_disabled;
};

static ThemeColors GetTheme() {
    ThemeColors t{};
    t.text = ImVec4(0.925f, 0.915f, 0.960f, 1.0f);
    t.text_disabled = ImVec4(0.502f, 0.478f, 0.588f, 1.0f);
    t.card_bg = ImVec4(1, 1, 1, 0.035f);
    t.card_bg_hover = ImVec4(1, 1, 1, 0.07f);

    switch (g_theme) {
    case ThemeMode::Blue:
        t.window_bg = ImVec4(0.024f, 0.035f, 0.078f, 1);
        t.topbar_bg = ImVec4(0.024f, 0.035f, 0.078f, 1);
        t.accent = ImVec4(0.35f, 0.60f, 1.00f, 1);
        t.accent2 = ImVec4(0.60f, 0.80f, 1.00f, 1);
        t.accent3 = ImVec4(0.20f, 0.45f, 0.90f, 1);
        t.accent_hover = ImVec4(0.45f, 0.70f, 1.00f, 1);
        t.accent_active = ImVec4(0.25f, 0.50f, 0.90f, 1); break;
    case ThemeMode::Purple:
        t.window_bg = ImVec4(0.039f, 0.027f, 0.063f, 1);
        t.topbar_bg = ImVec4(0.039f, 0.027f, 0.063f, 1);
        t.accent = ImVec4(0.627f, 0.420f, 1.000f, 1);
        t.accent2 = ImVec4(0.980f, 0.400f, 0.650f, 1);
        t.accent3 = ImVec4(0.380f, 0.700f, 1.000f, 1);
        t.accent_hover = ImVec4(0.72f, 0.53f, 1.00f, 1);
        t.accent_active = ImVec4(0.52f, 0.33f, 0.90f, 1); break;
    case ThemeMode::Green:
        t.window_bg = ImVec4(0.024f, 0.063f, 0.039f, 1);
        t.topbar_bg = ImVec4(0.024f, 0.063f, 0.039f, 1);
        t.accent = ImVec4(0.30f, 0.85f, 0.50f, 1);
        t.accent2 = ImVec4(0.60f, 1.00f, 0.70f, 1);
        t.accent3 = ImVec4(0.15f, 0.60f, 0.35f, 1);
        t.accent_hover = ImVec4(0.40f, 0.95f, 0.60f, 1);
        t.accent_active = ImVec4(0.20f, 0.70f, 0.40f, 1); break;
    case ThemeMode::Red:
        t.window_bg = ImVec4(0.063f, 0.024f, 0.035f, 1);
        t.topbar_bg = ImVec4(0.063f, 0.024f, 0.035f, 1);
        t.accent = ImVec4(1.00f, 0.35f, 0.40f, 1);
        t.accent2 = ImVec4(1.00f, 0.60f, 0.60f, 1);
        t.accent3 = ImVec4(0.80f, 0.20f, 0.30f, 1);
        t.accent_hover = ImVec4(1.00f, 0.45f, 0.50f, 1);
        t.accent_active = ImVec4(0.85f, 0.25f, 0.30f, 1); break;
    case ThemeMode::Orange:
        t.window_bg = ImVec4(0.063f, 0.039f, 0.016f, 1);
        t.topbar_bg = ImVec4(0.063f, 0.039f, 0.016f, 1);
        t.accent = ImVec4(1.00f, 0.60f, 0.20f, 1);
        t.accent2 = ImVec4(1.00f, 0.80f, 0.50f, 1);
        t.accent3 = ImVec4(0.85f, 0.45f, 0.10f, 1);
        t.accent_hover = ImVec4(1.00f, 0.70f, 0.30f, 1);
        t.accent_active = ImVec4(0.85f, 0.50f, 0.15f, 1); break;
    case ThemeMode::Pink:
        t.window_bg = ImVec4(0.078f, 0.027f, 0.063f, 1);
        t.topbar_bg = ImVec4(0.078f, 0.027f, 0.063f, 1);
        t.accent = ImVec4(1.00f, 0.45f, 0.75f, 1);
        t.accent2 = ImVec4(1.00f, 0.70f, 0.85f, 1);
        t.accent3 = ImVec4(0.85f, 0.35f, 0.65f, 1);
        t.accent_hover = ImVec4(1.00f, 0.55f, 0.85f, 1);
        t.accent_active = ImVec4(0.85f, 0.35f, 0.65f, 1); break;
    case ThemeMode::Teal:
        t.window_bg = ImVec4(0.016f, 0.063f, 0.063f, 1);
        t.topbar_bg = ImVec4(0.016f, 0.063f, 0.063f, 1);
        t.accent = ImVec4(0.20f, 0.90f, 0.90f, 1);
        t.accent2 = ImVec4(0.60f, 1.00f, 1.00f, 1);
        t.accent3 = ImVec4(0.15f, 0.70f, 0.70f, 1);
        t.accent_hover = ImVec4(0.30f, 1.00f, 1.00f, 1);
        t.accent_active = ImVec4(0.15f, 0.75f, 0.75f, 1); break;
    case ThemeMode::Yellow:
        t.window_bg = ImVec4(0.063f, 0.063f, 0.016f, 1);
        t.topbar_bg = ImVec4(0.063f, 0.063f, 0.016f, 1);
        t.accent = ImVec4(0.95f, 0.90f, 0.30f, 1);
        t.accent2 = ImVec4(1.00f, 0.95f, 0.60f, 1);
        t.accent3 = ImVec4(0.80f, 0.75f, 0.20f, 1);
        t.accent_hover = ImVec4(1.00f, 0.95f, 0.40f, 1);
        t.accent_active = ImVec4(0.80f, 0.75f, 0.20f, 1); break;
    case ThemeMode::Gray:
        t.window_bg = ImVec4(0.055f, 0.055f, 0.055f, 1);
        t.topbar_bg = ImVec4(0.055f, 0.055f, 0.055f, 1);
        t.accent = ImVec4(0.65f, 0.65f, 0.65f, 1);
        t.accent2 = ImVec4(0.85f, 0.85f, 0.85f, 1);
        t.accent3 = ImVec4(0.45f, 0.45f, 0.45f, 1);
        t.accent_hover = ImVec4(0.80f, 0.80f, 0.80f, 1);
        t.accent_active = ImVec4(0.55f, 0.55f, 0.55f, 1); break;
    case ThemeMode::Sunset:
        t.window_bg = ImVec4(0.078f, 0.027f, 0.043f, 1);
        t.topbar_bg = ImVec4(0.078f, 0.027f, 0.043f, 1);
        t.accent = ImVec4(1.00f, 0.55f, 0.25f, 1);
        t.accent2 = ImVec4(1.00f, 0.40f, 0.55f, 1);
        t.accent3 = ImVec4(0.85f, 0.40f, 0.15f, 1);
        t.accent_hover = ImVec4(1.00f, 0.35f, 0.55f, 1);
        t.accent_active = ImVec4(0.85f, 0.40f, 0.15f, 1); break;
    case ThemeMode::Ocean:
        t.window_bg = ImVec4(0.016f, 0.039f, 0.078f, 1);
        t.topbar_bg = ImVec4(0.016f, 0.039f, 0.078f, 1);
        t.accent = ImVec4(0.20f, 0.60f, 0.95f, 1);
        t.accent2 = ImVec4(0.40f, 0.80f, 1.00f, 1);
        t.accent3 = ImVec4(0.15f, 0.45f, 0.85f, 1);
        t.accent_hover = ImVec4(0.30f, 0.75f, 1.00f, 1);
        t.accent_active = ImVec4(0.15f, 0.45f, 0.85f, 1); break;
    case ThemeMode::Forest:
        t.window_bg = ImVec4(0.027f, 0.055f, 0.027f, 1);
        t.topbar_bg = ImVec4(0.027f, 0.055f, 0.027f, 1);
        t.accent = ImVec4(0.40f, 0.80f, 0.35f, 1);
        t.accent2 = ImVec4(0.70f, 1.00f, 0.60f, 1);
        t.accent3 = ImVec4(0.30f, 0.65f, 0.25f, 1);
        t.accent_hover = ImVec4(0.55f, 0.95f, 0.45f, 1);
        t.accent_active = ImVec4(0.30f, 0.65f, 0.25f, 1); break;
    case ThemeMode::Dark:
    default:
        t.window_bg = ImVec4(0.035f, 0.024f, 0.058f, 1);
        t.topbar_bg = ImVec4(0.035f, 0.024f, 0.058f, 1);
        t.accent = ImVec4(0.627f, 0.420f, 1.000f, 1);
        t.accent2 = ImVec4(0.980f, 0.400f, 0.650f, 1);
        t.accent3 = ImVec4(0.380f, 0.700f, 1.000f, 1);
        t.accent_hover = ImVec4(0.72f, 0.53f, 1.00f, 1);
        t.accent_active = ImVec4(0.52f, 0.33f, 0.90f, 1); break;
    }
    return t;
}

static ImU32 ColF(ImVec4 c, float a) {
    return IM_COL32((int)(c.x * 255), (int)(c.y * 255), (int)(c.z * 255), (int)(MY_CLAMP(a, 0.0f, 1.0f) * 255));
}

// Открыть страницу игры в Steam по AppID
static void OpenGameInSteam(const std::string& appid) {
    if (appid.empty()) return;
    std::string url = "steam://store/" + appid;
    ShellExecuteA(nullptr, "open", url.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
}

// ============================================================
//  Steam-механика
// ============================================================
static void RunHidden(const std::string& cmd) {
    STARTUPINFOA si = {};
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESHOWWINDOW;
    si.wShowWindow = SW_HIDE;
    PROCESS_INFORMATION pi = {};
    std::string m = cmd;
    if (CreateProcessA(nullptr, m.data(), nullptr, nullptr, FALSE,
        CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi)) {
        WaitForSingleObject(pi.hProcess, INFINITE);
        CloseHandle(pi.hProcess);
        CloseHandle(pi.hThread);
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

static void CloseSteam() {
    RunHidden("taskkill /f /im steam.exe");
    RunHidden("taskkill /f /im steamwebhelper.exe");
    Sleep(1200);
}

static void RestartSteam() {
    if (g_app_steam_path.empty()) return;
    CloseSteam();
    std::string exe = g_app_steam_path + "\\Steam.exe";
    ShellExecuteA(nullptr, "open", exe.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
}

static void OpenLuaFolder() {
    if (g_app_steam_path.empty()) return;
    fs::path lua = fs::path(g_app_steam_path) / "config" / "lua";
    std::error_code ec;
    fs::create_directories(lua, ec);
    ShellExecuteW(nullptr, L"open", lua.wstring().c_str(), nullptr, nullptr, SW_SHOWNORMAL);
}

static bool HttpGetBinary(const std::string& url, std::vector<unsigned char>& out) {
    std::wstring wurl = Utf8ToWide(url);
    URL_COMPONENTS uc = {};
    uc.dwStructSize = sizeof(uc);
    wchar_t host[256] = {}, path[2048] = {};
    uc.lpszHostName = host; uc.dwHostNameLength = 255;
    uc.lpszUrlPath = path;  uc.dwUrlPathLength = 2047;
    if (!WinHttpCrackUrl(wurl.c_str(), 0, 0, &uc)) return false;
    bool https = (uc.nScheme == INTERNET_SCHEME_HTTPS);

    HINTERNET hSession = WinHttpOpen(L"SLT/9.1", WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY,
        WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (!hSession) return false;
    HINTERNET hConnect = WinHttpConnect(hSession, host, uc.nPort, 0);
    if (!hConnect) { WinHttpCloseHandle(hSession); return false; }
    DWORD flags = https ? WINHTTP_FLAG_SECURE : 0;
    HINTERNET hRequest = WinHttpOpenRequest(hConnect, L"GET", path, nullptr,
        WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, flags);
    if (!hRequest) { WinHttpCloseHandle(hConnect); WinHttpCloseHandle(hSession); return false; }
    if (!WinHttpSendRequest(hRequest, WINHTTP_NO_ADDITIONAL_HEADERS, 0,
        WINHTTP_NO_REQUEST_DATA, 0, 0, 0) ||
        !WinHttpReceiveResponse(hRequest, nullptr)) {
        WinHttpCloseHandle(hRequest); WinHttpCloseHandle(hConnect); WinHttpCloseHandle(hSession); return false;
    }
    DWORD status = 0, sz = sizeof(status);
    WinHttpQueryHeaders(hRequest, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
        WINHTTP_HEADER_NAME_BY_INDEX, &status, &sz, WINHTTP_NO_HEADER_INDEX);
    if (status != 200) {
        WinHttpCloseHandle(hRequest); WinHttpCloseHandle(hConnect); WinHttpCloseHandle(hSession); return false;
    }
    out.clear();
    DWORD avail = 0;
    do {
        avail = 0;
        if (!WinHttpQueryDataAvailable(hRequest, &avail)) break;
        if (avail == 0) break;
        size_t old = out.size();
        out.resize(old + avail);
        DWORD read = 0;
        if (!WinHttpReadData(hRequest, out.data() + old, avail, &read)) break;
        out.resize(old + read);
    } while (avail > 0);
    WinHttpCloseHandle(hRequest);
    WinHttpCloseHandle(hConnect);
    WinHttpCloseHandle(hSession);
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
//  Данные
// ============================================================
struct GameEntry {
    std::string appid;
    std::string name;
    std::string header_image;

    ID3D11ShaderResourceView* texture = nullptr;
    bool loading = false;
    bool load_failed = false;
    float fade_in = 0.0f;
    float open_time = -1.0f;
    bool installed = false;
};

struct PendingTexture {
    std::string appid;
    int w = 0, h = 0;
    std::vector<unsigned char> pixels;
};

struct LogEntry {
    std::string text;
    bool is_error = false;
    std::chrono::system_clock::time_point time;
};

enum class ProgressMode {
    None = 0,
    Download,
    Delete,
    InstallTools,
};

struct AppState {
    std::vector<GameEntry> all_games;
    std::vector<int>       filtered;

    char search_buf[128] = "";
    std::string search_query;

    int page = 0;
    static const int kGamesPerPage = 20;
    static const int kGridCols = 5;
    static const int kGridRows = 4;

    bool steam_found = false;

    struct InstalledEntry { std::string appid; std::string name; };
    std::vector<InstalledEntry> installed;

    std::deque<LogEntry> log;
    std::mutex log_mtx;

    std::atomic<int>   progress_mode{ (int)ProgressMode::None };
    std::atomic<bool>  cancel_requested{ false };
    std::atomic<float> progress_target{ 0.0f };
    float              progress_display = 0.0f;
    std::string        current_name;
    std::string        current_appid;
    std::mutex         status_mtx;

    bool  success_visible = false;
    float success_alpha = 0.0f;
    float success_timer = 0.0f;
    float overlay_alpha = 0.0f;

    int current_tab = 0;
    float underline_x = 0.0f;
    int   underline_initialized = 0;
    float time_accum = 0.0f;
    float app_open_anim = 0.0f;
    int   notifications_count = 0;

    ImVec2 outline_pos_cur = ImVec2(0, 0);
    ImVec2 outline_size_cur = ImVec2(0, 0);
    ImVec2 outline_pos_target = ImVec2(0, 0);
    ImVec2 outline_size_target = ImVec2(0, 0);
    float  outline_alpha = 0.0f;
    bool   outline_any_hover = false;

    std::deque<PendingTexture> pending_textures;
    std::mutex                 pending_mutex;
    std::atomic<int>           active_downloads{ 0 };

    bool IsBusy() const {
        return progress_mode.load() != (int)ProgressMode::None;
    }
};
static AppState g_app;

// ============================================================
//  Лог
// ============================================================
static void LogPush(const std::string& text, bool is_error = false) {
    std::lock_guard<std::mutex> lk(g_app.log_mtx);
    LogEntry e;
    e.text = text; e.is_error = is_error;
    e.time = std::chrono::system_clock::now();
    g_app.log.push_front(e);
    if (g_app.log.size() > 200) g_app.log.pop_back();
    g_app.notifications_count = (int)g_app.log.size();
}

// ============================================================
//  Настройки
// ============================================================
static void SaveSettings() {
    try {
        json j;
        j["theme"] = (int)g_theme;
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
        if (j.contains("theme")) {
            int v = j["theme"].get<int>();
            if (v >= 0 && v <= (int)ThemeMode::Forest)
                g_theme = (ThemeMode)v;
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
    s.WindowRounding = 0.0f;
    s.ChildRounding = 10.0f;
    s.FrameRounding = 8.0f;
    s.PopupRounding = 10.0f;
    s.ScrollbarRounding = 10.0f;
    s.GrabRounding = 8.0f;
    s.WindowBorderSize = 0.0f;
    s.FrameBorderSize = 0.0f;
    s.ItemSpacing = ImVec2(12, 10);
    s.FramePadding = ImVec2(12, 8);

    ImVec4* c = s.Colors;
    c[ImGuiCol_WindowBg] = t.window_bg;
    c[ImGuiCol_ChildBg] = ImVec4(0, 0, 0, 0);
    c[ImGuiCol_FrameBg] = ImVec4(1, 1, 1, 0.05f);
    c[ImGuiCol_FrameBgHovered] = ImVec4(1, 1, 1, 0.08f);
    c[ImGuiCol_FrameBgActive] = ImVec4(1, 1, 1, 0.11f);
    c[ImGuiCol_Text] = t.text;
    c[ImGuiCol_TextDisabled] = t.text_disabled;
    c[ImGuiCol_Button] = ImVec4(1, 1, 1, 0.05f);
    c[ImGuiCol_ButtonHovered] = ImVec4(t.accent.x, t.accent.y, t.accent.z, 0.30f);
    c[ImGuiCol_ButtonActive] = ImVec4(t.accent.x, t.accent.y, t.accent.z, 0.50f);
}

// ============================================================
//  Фильтр мусора Steam (по AppID + узкий список имён)
// ============================================================
static bool IsJunkGame(const std::string& name, const std::string& appid) {
    static const std::vector<std::string> junk_ids = {
        "228980",
        "1070560", "1391110", "1623560", "2180100",
        "961370", "1007", "7",
    };
    for (auto& id : junk_ids) if (appid == id) return true;

    std::string n = name;
    for (auto& c : n) c = (char)tolower((unsigned char)c);

    static const std::vector<std::string> bad_exact = {
        "source sdk base",
        "source sdk",
        "dedicated server",
        "steamvr",
        "steam linux runtime",
        "steamworks common",
        "proton experimental",
        "steam client",
    };
    for (auto& b : bad_exact)
        if (n.find(b) != std::string::npos) return true;

    if (n.size() < 2) return true;
    return false;
}

// ============================================================
//  Загрузка JSON
// ============================================================
static bool LoadGamesJson() {
    const char* candidates[] = { "gamespic.json", "./gamespic.json", "../gamespic.json" };
    std::ifstream f;
    for (auto p : candidates) {
        if (fs::exists(p)) { f.open(p, std::ios::binary); break; }
    }

    std::string json_text;

    if (f.is_open()) {
        std::stringstream ss;
        ss << f.rdbuf();
        json_text = ss.str();
    }
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

    g_app.all_games.clear();

    auto parseOne = [&](const json& it) {
        if (!it.contains("appid") || !it.contains("name")) return;
        std::string appid = it.value("appid", "");
        std::string type = it.value("type", "game");
        std::string name = it.value("name", "");

        // Всё в нижний регистр, чтобы "Game" и "game" совпадали
        std::string type_l = type;
        for (auto& c : type_l) c = (char)tolower((unsigned char)c);
        if (!type_l.empty() && type_l != "game") return;

        if (IsJunkGame(name, appid)) return;

        GameEntry g;
        g.appid = appid;
        g.name = name;
        g.header_image = it.value("header_image", "");
        g_app.all_games.push_back(std::move(g));
        };

    if (j.is_array()) for (auto& item : j) parseOne(item);
    else if (j.is_object()) for (auto& kv : j.items()) if (kv.value().is_object()) parseOne(kv.value());

    return !g_app.all_games.empty();
}

// ============================================================
//  Скачивание картинок
// ============================================================
static void DownloadAndDecodeWorker(std::string url, std::string appid) {
    std::vector<unsigned char> raw;
    bool ok = HttpGetBinary(url, raw);
    if (ok) {
        int w, h, comp;
        unsigned char* pixels = stbi_load_from_memory(raw.data(), (int)raw.size(), &w, &h, &comp, 4);
        if (pixels) {
            PendingTexture pt;
            pt.appid = appid;
            pt.w = w; pt.h = h;
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
    g.loading = true;
    g_app.active_downloads++;
    std::thread(DownloadAndDecodeWorker, g.header_image, g.appid).detach();
}

static void ProcessPendingTextures() {
    int done = 0;
    while (done < 3) {
        PendingTexture pt;
        {
            std::lock_guard<std::mutex> lock(g_app.pending_mutex);
            if (g_app.pending_textures.empty()) break;
            pt = std::move(g_app.pending_textures.front());
            g_app.pending_textures.pop_front();
        }
        D3D11_TEXTURE2D_DESC desc = {};
        desc.Width = pt.w; desc.Height = pt.h;
        desc.MipLevels = 1; desc.ArraySize = 1;
        desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        desc.SampleDesc.Count = 1;
        desc.Usage = D3D11_USAGE_DEFAULT;
        desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
        D3D11_SUBRESOURCE_DATA sub = {};
        sub.pSysMem = pt.pixels.data();
        sub.SysMemPitch = pt.w * 4;
        ID3D11Texture2D* tex = nullptr;
        if (SUCCEEDED(g_pd3dDevice->CreateTexture2D(&desc, &sub, &tex))) {
            ID3D11ShaderResourceView* srv = nullptr;
            if (SUCCEEDED(g_pd3dDevice->CreateShaderResourceView(tex, nullptr, &srv))) {
                for (auto& g : g_app.all_games) {
                    if (g.appid == pt.appid) {
                        g.texture = srv;
                        g.open_time = (float)ImGui::GetTime();
                        g.loading = false;
                        break;
                    }
                }
            }
            tex->Release();
        }
        done++;
    }
}

// ============================================================
//  Поиск и фильтрация
// ============================================================
static void RebuildFiltered() {
    g_app.filtered.clear();
    std::string q = g_app.search_query;
    std::transform(q.begin(), q.end(), q.begin(), ::tolower);

    for (int i = 0; i < (int)g_app.all_games.size(); i++) {
        if (q.empty()) { g_app.filtered.push_back(i); continue; }
        std::string name = g_app.all_games[i].name;
        std::transform(name.begin(), name.end(), name.begin(), ::tolower);
        bool match_name = name.find(q) != std::string::npos;
        bool match_id = g_app.all_games[i].appid.find(q) != std::string::npos;
        if (match_name || match_id) g_app.filtered.push_back(i);
    }
    g_app.page = 0;
}

// ============================================================
//  Steam-манифесты
// ============================================================
static void RefreshInstalledFlags() {
    if (g_app_steam_path.empty()) return;
    fs::path lua_dir = fs::path(g_app_steam_path) / "config" / "lua";
    std::map<std::string, bool> installed;
    if (fs::exists(lua_dir)) {
        for (auto& entry : fs::directory_iterator(lua_dir))
            if (entry.path().extension() == ".lua")
                installed[entry.path().stem().string()] = true;
    }
    for (auto& g : g_app.all_games)
        g.installed = installed.count(g.appid) > 0;
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
            for (auto& g : g_app.all_games)
                if (g.appid == e.appid) { e.name = g.name; break; }
            g_app.installed.push_back(e);
        }
    }
}

static void DeleteInstalled(const std::string& appid) {
    if (g_app_steam_path.empty()) return;
    fs::path lua = fs::path(g_app_steam_path) / "config" / "lua";
    fs::path depot = fs::path(g_app_steam_path) / "depotcache";
    std::error_code ec;
    if (fs::exists(lua))
        for (auto& p : fs::directory_iterator(lua))
            if (p.path().stem().string() == appid) fs::remove(p.path(), ec);
    if (fs::exists(depot))
        for (auto& p : fs::directory_iterator(depot))
            if (p.path().stem().string() == appid) fs::remove(p.path(), ec);
    RefreshInstalledFiles();
    RefreshInstalledFlags();
    LogPush("Удалена игра AppID: " + appid);
}

static int CopyLuaFromDir(const std::string& extract_dir) {
    fs::path lua_dst = fs::path(g_app_steam_path) / "config" / "lua";
    fs::path depot_dst = fs::path(g_app_steam_path) / "depotcache";
    fs::create_directories(lua_dst);
    fs::create_directories(depot_dst);
    int copied = 0;
    for (auto& p : fs::recursive_directory_iterator(extract_dir)) {
        if (!p.is_regular_file()) continue;
        auto ext = p.path().extension().string();
        if (ext == ".lua" || ext == ".manifest") {
            std::error_code ec;
            fs::copy_file(p.path(), lua_dst / p.path().filename(),
                fs::copy_options::overwrite_existing, ec);
            fs::copy_file(p.path(), depot_dst / p.path().filename(),
                fs::copy_options::overwrite_existing, ec);
            copied++;
        }
    }
    return copied;
}

static void ShowSuccess() {
    g_app.success_visible = true;
    g_app.success_alpha = 0.0f;
    g_app.success_timer = 0.0f;
}

// ============================================================
//  Скачать манифест
// ============================================================
static void DownloadManifestAsync(std::string appid, std::string name) {
    if (g_app.IsBusy()) { LogPush("Уже идёт операция", true); return; }

    g_app.progress_mode.store((int)ProgressMode::Download);
    g_app.cancel_requested = false;
    g_app.progress_target = 0.01f;
    g_app.progress_display = 0.0f;
    {
        std::lock_guard<std::mutex> lk(g_app.status_mtx);
        g_app.current_name = name + " (AppID " + appid + ")";
        g_app.current_appid = appid;
    }
    LogPush("Начата загрузка: " + name + " [AppID " + appid + "]");

    std::thread([appid, name]() {
        std::string url = "https://codeload.github.com/SPIN0ZAi/SB_manifest_DB/zip/refs/heads/" + appid;
        char temp_path[MAX_PATH]; GetTempPathA(MAX_PATH, temp_path);
        std::string zip_path = std::string(temp_path) + "SLT_" + appid + ".zip";
        std::string extract_dir = std::string(temp_path) + "SLT_" + appid;

        auto cleanup_partial = [&]() {
            std::error_code ec;
            fs::remove(zip_path, ec);
            fs::remove_all(extract_dir, ec);
            };

        g_app.progress_target = 0.15f;
        bool ok = DownloadToFile(url, zip_path);

        if (g_app.cancel_requested) {
            cleanup_partial();
            LogPush("Загрузка отменена: " + name, true);
            g_app.progress_mode.store((int)ProgressMode::None);
            g_app.progress_target = 0.0f;
            g_app.progress_display = 0.0f;
            return;
        }

        g_app.progress_target = 0.65f;
        if (!ok) {
            LogPush("Не удалось скачать манифест для " + name, true);
            cleanup_partial();
            g_app.progress_mode.store((int)ProgressMode::None);
            g_app.progress_target = 0.0f;
            g_app.progress_display = 0.0f;
            return;
        }
        LogPush("Скачано: " + name + ", распаковка...");

        std::error_code ec;
        fs::remove_all(extract_dir, ec);
        std::string cmd = "powershell -NoProfile -Command \"Expand-Archive -Force -Path '" +
            zip_path + "' -DestinationPath '" + extract_dir + "'\"";
        RunHidden(cmd);

        if (g_app.cancel_requested) {
            cleanup_partial();
            LogPush("Загрузка отменена: " + name, true);
            g_app.progress_mode.store((int)ProgressMode::None);
            g_app.progress_target = 0.0f;
            g_app.progress_display = 0.0f;
            return;
        }

        g_app.progress_target = 0.88f;
        int copied = CopyLuaFromDir(extract_dir);
        LogPush("Установлено файлов: " + std::to_string(copied) + " для " + name);

        cleanup_partial();

        g_app.progress_target = 1.0f;
        std::this_thread::sleep_for(std::chrono::milliseconds(300));
        RefreshInstalledFlags();
        RefreshInstalledFiles();

        g_app.progress_mode.store((int)ProgressMode::None);
        g_app.progress_target = 0.0f;
        g_app.progress_display = 0.0f;
        ShowSuccess();
        }).detach();
}

static void CancelCurrentDownload() {
    if (g_app.progress_mode.load() != (int)ProgressMode::Download) return;
    g_app.cancel_requested = true;

    std::string appid;
    {
        std::lock_guard<std::mutex> lk(g_app.status_mtx);
        appid = g_app.current_appid;
    }
    if (appid.empty()) {
        g_app.progress_mode.store((int)ProgressMode::None);
        return;
    }

    g_app.progress_mode.store((int)ProgressMode::Delete);
    g_app.progress_target = 0.01f;
    g_app.progress_display = 0.0f;

    std::thread([appid]() {
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
        g_app.progress_target = 0.35f;

        fs::path lua = fs::path(g_app_steam_path) / "config" / "lua";
        fs::path depot = fs::path(g_app_steam_path) / "depotcache";
        std::error_code ec;

        std::vector<fs::path> to_delete;
        if (fs::exists(lua))
            for (auto& p : fs::directory_iterator(lua))
                if (p.path().stem().string() == appid) to_delete.push_back(p.path());
        if (fs::exists(depot))
            for (auto& p : fs::directory_iterator(depot))
                if (p.path().stem().string() == appid) to_delete.push_back(p.path());

        int total = (int)to_delete.size();
        if (total == 0) {
            g_app.progress_target = 1.0f;
        }
        else {
            for (int i = 0; i < total; ++i) {
                fs::remove(to_delete[i], ec);
                float pr = 0.35f + 0.65f * ((float)(i + 1) / total);
                g_app.progress_target = pr;
                std::this_thread::sleep_for(std::chrono::milliseconds(150));
            }
        }

        LogPush("Отмена: удалены файлы AppID " + appid, true);
        RefreshInstalledFiles();
        RefreshInstalledFlags();

        g_app.progress_target = 1.0f;
        std::this_thread::sleep_for(std::chrono::milliseconds(400));

        g_app.progress_mode.store((int)ProgressMode::None);
        g_app.progress_target = 0.0f;
        g_app.progress_display = 0.0f;
        ShowSuccess();
        }).detach();
}

static void InstallZipFile(const std::string& zip_path) {
    if (g_app_steam_path.empty()) { LogPush("Steam не найден", true); return; }
    char temp_path[MAX_PATH]; GetTempPathA(MAX_PATH, temp_path);
    std::string extract_dir = std::string(temp_path) + "SLT_manual";
    std::error_code ec;
    fs::remove_all(extract_dir, ec);
    std::string cmd = "powershell -NoProfile -Command \"Expand-Archive -Force -Path '" +
        zip_path + "' -DestinationPath '" + extract_dir + "'\"";
    RunHidden(cmd);
    int copied = CopyLuaFromDir(extract_dir);
    LogPush("Установлено файлов из ZIP: " + std::to_string(copied));
    fs::remove_all(extract_dir, ec);
    RefreshInstalledFiles();
    RefreshInstalledFlags();
}

static void InstallOpenSteamToolsAsync() {
    if (g_app.IsBusy()) { LogPush("Уже идёт операция", true); return; }

    g_app.progress_mode.store((int)ProgressMode::InstallTools);
    g_app.progress_target = 0.05f;
    g_app.progress_display = 0.0f;
    {
        std::lock_guard<std::mutex> lk(g_app.status_mtx);
        g_app.current_name = "OpenSteamTools";
        g_app.current_appid = "";
    }
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
            if (!hRes) {
                LogPush("OpenSteamTools.zip не найден", true);
                g_app.progress_mode.store((int)ProgressMode::None);
                g_app.progress_target = 0.0f;
                return;
            }
            HGLOBAL hData = LoadResource(nullptr, hRes);
            DWORD size = SizeofResource(nullptr, hRes);
            const void* ptr = hData ? LockResource(hData) : nullptr;
            if (!ptr) {
                LogPush("Не удалось загрузить ресурс OpenSteamTools.zip", true);
                g_app.progress_mode.store((int)ProgressMode::None);
                g_app.progress_target = 0.0f;
                return;
            }
            std::ofstream of(zip_path, std::ios::binary);
            of.write((const char*)ptr, size);
            of.close();
            zip = zip_path;
        }

        g_app.progress_target = 0.20f;
        CloseSteam();

        g_app.progress_target = 0.45f;
        std::error_code ec;
        fs::remove_all(extract_dir, ec);
        std::string cmd = "powershell -NoProfile -Command \"Expand-Archive -Force -Path '" +
            fs::absolute(zip).string() + "' -DestinationPath '" + extract_dir + "'\"";
        RunHidden(cmd);

        g_app.progress_target = 0.70f;
        int copied = 0;
        for (auto& p : fs::recursive_directory_iterator(extract_dir)) {
            if (!p.is_regular_file()) continue;
            try {
                fs::copy_file(p.path(),
                    fs::path(g_app_steam_path) / p.path().filename(),
                    fs::copy_options::overwrite_existing);
                copied++;
            }
            catch (...) {}
        }
        fs::remove_all(extract_dir, ec);
        if (zip == zip_path) fs::remove(zip_path, ec);

        g_app.progress_target = 0.95f;
        LogPush("OpenSteamTools: скопировано " + std::to_string(copied) + " файлов");

        std::this_thread::sleep_for(std::chrono::milliseconds(300));
        g_app.progress_target = 1.0f;
        std::this_thread::sleep_for(std::chrono::milliseconds(400));

        g_app.progress_mode.store((int)ProgressMode::None);
        g_app.progress_target = 0.0f;
        g_app.progress_display = 0.0f;
        ShowSuccess();
        }).detach();
}

// ============================================================
//  Аврора-фон
// ============================================================
static void DrawAuroraBackground(ImDrawList* dl, ImVec2 origin, ImVec2 size, float time) {
    ThemeColors t = GetTheme();
    dl->AddRectFilled(origin, ImVec2(origin.x + size.x, origin.y + size.y), ColF(t.window_bg, 1.0f));

    struct Wave { float y0, amp, freq, speed, thickness; ImVec4 col; };
    Wave waves[3] = {
        { size.y * 0.28f, 60.0f, 1.6f, 0.15f, 140.0f, t.accent  },
        { size.y * 0.55f, 80.0f, 1.1f, -0.11f, 170.0f, t.accent2 },
        { size.y * 0.78f, 50.0f, 2.0f, 0.09f, 120.0f, t.accent3 },
    };

    const int kTrails = 7;
    for (auto& w : waves) {
        for (int trail = kTrails - 1; trail >= 0; trail--) {
            float trail_t = time - trail * 0.035f;
            float alpha = (1.0f - (float)trail / kTrails) * 0.05f;

            ImVec2 pts[64];
            int n = 64;
            for (int i = 0; i < n; i++) {
                float fx = (float)i / (n - 1);
                float x = origin.x + fx * size.x;
                float y = origin.y + w.y0
                    + std::sin(fx * w.freq * 6.2831853f + trail_t * w.speed * 6.2831853f) * w.amp
                    + std::sin(fx * w.freq * 2.0f + trail_t * w.speed * 3.0f) * w.amp * 0.3f;
                pts[i] = ImVec2(x, y);
            }
            for (int i = 0; i < n - 1; i++) {
                dl->AddLine(pts[i], pts[i + 1], ColF(w.col, alpha), w.thickness * (0.4f + 0.6f * ((float)i / n)));
            }
        }
    }

    dl->AddRectFilledMultiColor(origin, ImVec2(origin.x + size.x, origin.y + 120),
        ColF(t.window_bg, 0.55f), ColF(t.window_bg, 0.55f), ColF(t.window_bg, 0.0f), ColF(t.window_bg, 0.0f));
}

// ============================================================
//  Кнопки окна
// ============================================================
static void DrawWindowButton(ImDrawList* dl, ImVec2 p0, ImVec2 p1, bool is_close, bool hovered, bool active) {
    ImVec4 base = is_close ? ImVec4(0.85f, 0.22f, 0.32f, 1) : ImVec4(1, 1, 1, 1);
    float bg_alpha = active ? (is_close ? 1.0f : 0.18f) : (hovered ? (is_close ? 0.9f : 0.12f) : 0.0f);
    dl->AddRectFilled(p0, p1, ColF(base, bg_alpha), 6.0f);

    ImVec2 c = ImVec2((p0.x + p1.x) * 0.5f, (p0.y + p1.y) * 0.5f);
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
//  Топбар
// ============================================================
static void DrawTopBar() {
    ThemeColors t = GetTheme();
    ImGui::PushStyleColor(ImGuiCol_ChildBg, t.topbar_bg);
    const float topbar_h = 60.0f;
    ImGui::BeginChild("##topbar", ImVec2(0, topbar_h), false, ImGuiWindowFlags_NoScrollbar);

    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImVec2 wp = ImGui::GetWindowPos();
    ImVec2 ws = ImGui::GetWindowSize();
    float time = (float)ImGui::GetTime();

    dl->AddRectFilledMultiColor(
        ImVec2(wp.x, wp.y + ws.y - 1), ImVec2(wp.x + ws.x, wp.y + ws.y),
        ColF(t.accent, 0.5f), ColF(t.accent3, 0.5f), ColF(t.accent2, 0.5f), ColF(t.accent, 0.5f));

    static bool s_dragging = false;
    static POINT s_start_mouse{}, s_start_window{};
    ImVec2 mp = ImGui::GetIO().MousePos;
    bool in_drag_zone = mp.x >= wp.x && mp.x <= wp.x + ws.x - 220 && mp.y >= wp.y && mp.y <= wp.y + 50;
    if (!s_dragging && in_drag_zone && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
        s_dragging = true; GetCursorPos(&s_start_mouse);
        RECT rc; GetWindowRect(g_hwnd, &rc); s_start_window = { rc.left, rc.top };
    }
    if (s_dragging && ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
        POINT p; GetCursorPos(&p);
        SetWindowPos(g_hwnd, nullptr, s_start_window.x + (p.x - s_start_mouse.x),
            s_start_window.y + (p.y - s_start_mouse.y), 0, 0, SWP_NOSIZE | SWP_NOZORDER);
    }
    if (ImGui::IsMouseReleased(ImGuiMouseButton_Left)) s_dragging = false;

    
    // Название — сдвинуто левее (на место бывшего логотипа)
    ImGui::SetCursorPos(ImVec2(28, 18.0f));
    if (g_font_title) ImGui::PushFont(g_font_title);
    ImGui::TextColored(t.text, "SteamLuaTools");
    ImVec2 title_size = ImGui::CalcTextSize("SteamLuaTools");
    if (g_font_title) ImGui::PopFont();

    // Версия 9.1 в НЕОНОВОМ кружке, ровно по центру
    {
        ImVec2 vpos(wp.x + 28 + title_size.x + 20, wp.y + 18 + title_size.y * 0.5f);
        float vradius = 13.0f;

        float pulse = 0.85f + 0.15f * std::sin(time * 2.5f);

        // 1. Внешнее неоновое свечение (большой мягкий круг)
        dl->AddCircleFilled(vpos, vradius * 2.0f, ColF(t.accent, 0.10f * pulse), 48);
        dl->AddCircleFilled(vpos, vradius * 1.5f, ColF(t.accent, 0.18f * pulse), 48);

        // 2. Основной круг — с плотным фоном
        dl->AddCircleFilled(vpos, vradius, ColF(t.accent, 0.95f), 48);

        // 3. Яркая неоновая обводка
        dl->AddCircle(vpos, vradius, ColF(ImVec4(1, 1, 1, 1), 0.6f), 48, 2.0f);
        dl->AddCircle(vpos, vradius + 1.0f, ColF(t.accent_hover, 0.9f), 48, 1.5f);

        // 4. Текст "9.1" — ровно по центру
        const char* ver = "9.1";
        ImVec2 vsz = ImGui::CalcTextSize(ver);
        dl->AddText(
            ImVec2(vpos.x - vsz.x * 0.5f, vpos.y - vsz.y * 0.5f - 1.0f),
            IM_COL32(255, 255, 255, 255), ver);
    }

    const float tab_w = 140.0f, tab_h = 32.0f, tab_gap = 6.0f;
    const float tabs_y = (topbar_h - tab_h) * 0.5f;
    const float tabs_x = 280.0f;
    struct TabInfo { const char* label; int idx; bool has_badge; };
    TabInfo tabs[4] = {
        { "Магазин",      0, false },
        { "Библиотека",   1, false },
        { "Инструменты",  2, false },
        { "Уведомления",  3, g_app.notifications_count > 0 },
    };
    static float s_tab_x[4] = { 0 };
    static float s_tab_hover[4] = { 0,0,0,0 };
    float dt = ImGui::GetIO().DeltaTime; if (dt <= 0.0f) dt = 0.016f;
    bool busy = g_app.IsBusy();
    if (busy) ImGui::BeginDisabled();

    for (int i = 0; i < 4; i++) {
        float x = tabs_x + i * (tab_w + tab_gap);
        s_tab_x[i] = x;
        ImGui::SetCursorPos(ImVec2(x, tabs_y));
        ImGui::PushID(i);
        bool active = (g_app.current_tab == tabs[i].idx);

        ImVec2 p0 = ImVec2(wp.x + x, wp.y + tabs_y), p1 = ImVec2(p0.x + tab_w, p0.y + tab_h);
        bool hovered = ImGui::IsMouseHoveringRect(p0, p1);
        s_tab_hover[i] = ExpLerp(s_tab_hover[i], hovered ? 1.0f : 0.0f, 14.0f, dt);

        ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 6.0f);
        ImGui::PushStyleColor(ImGuiCol_Button, active ? ImVec4(t.accent.x, t.accent.y, t.accent.z, 0.20f) : ImVec4(1, 1, 1, 0.0f));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(t.accent.x, t.accent.y, t.accent.z, 0.28f));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(t.accent.x, t.accent.y, t.accent.z, 0.38f));
        ImGui::PushStyleColor(ImGuiCol_Text, active ? t.text : t.text_disabled);
        if (ImGui::Button(tabs[i].label, ImVec2(tab_w, tab_h))) g_app.current_tab = tabs[i].idx;
        ImGui::PopStyleColor(4); ImGui::PopStyleVar();

        if (tabs[i].has_badge) {
            float bp = 0.7f + 0.3f * std::sin(time * 4.0f);
            ImVec2 bc(p1.x - 14, p0.y + 8);
            dl->AddCircleFilled(bc, 7.0f * bp, ColF(t.accent2, 0.35f));
            dl->AddCircleFilled(bc, 4.0f, ColF(t.accent2, 1.0f));
        }
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
    float bx1_min = wp.x + ws.x - 32.0f - btn_w;
    float bx0_min = bx1_min - gap - btn_w;
    float by = wp.y + (topbar_h - btn_h) * 0.5f;

    ImVec2 min_p0(bx0_min, by), min_p1(bx0_min + btn_w, by + btn_h);
    ImVec2 cls_p0(bx1_min, by), cls_p1(bx1_min + btn_w, by + btn_h);

    bool min_hover = ImGui::IsMouseHoveringRect(min_p0, min_p1);
    bool cls_hover = ImGui::IsMouseHoveringRect(cls_p0, cls_p1);
    bool min_active = min_hover && ImGui::IsMouseDown(ImGuiMouseButton_Left);
    bool cls_active = cls_hover && ImGui::IsMouseDown(ImGuiMouseButton_Left);

    DrawWindowButton(dl, min_p0, min_p1, false, min_hover, min_active);
    DrawWindowButton(dl, cls_p0, cls_p1, true, cls_hover, cls_active);

    if (min_hover && ImGui::IsMouseReleased(ImGuiMouseButton_Left)) ShowWindow(g_hwnd, SW_MINIMIZE);
    if (cls_hover && ImGui::IsMouseReleased(ImGuiMouseButton_Left)) PostMessageW(g_hwnd, WM_CLOSE, 0, 0);

    ImGui::EndChild();
    ImGui::PopStyleColor();
}

// ============================================================
//  Страница МАГАЗИН
// ============================================================
static void DrawShopPage() {
    ThemeColors t = GetTheme();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    float dt = ImGui::GetIO().DeltaTime; if (dt <= 0.0f) dt = 0.016f;

    // Поиск — чуть левее от левого края
    ImGui::SetCursorPosX(ImGui::GetCursorPosX() + 24.0f);
    ImGui::SetNextItemWidth(360.0f);
    if (ImGui::InputTextWithHint("##search", "Поиск игры по названию или AppID...",
        g_app.search_buf, IM_ARRAYSIZE(g_app.search_buf))) {
        g_app.search_query = g_app.search_buf;
        RebuildFiltered();
    }

    ImGui::Spacing();
    ImGui::Spacing();

    if (g_app.all_games.empty()) {
        ImGui::Dummy(ImVec2(0, 40));
        ImGui::TextColored(t.text_disabled, "gamespic.json не найден рядом с программой.");
        return;
    }

    ImVec2 avail_region = ImGui::GetContentRegionAvail();
    float pager_h = 52.0f;
    float grid_h = avail_region.y - pager_h;

    // Отступы от краёв окна
    float side_pad = 24.0f;
    avail_region.x -= side_pad * 2.0f;

    float gap = 14.0f;
    float card_w = (avail_region.x - gap * (AppState::kGridCols - 1)) / AppState::kGridCols;
    float card_h = (grid_h - gap * (AppState::kGridRows - 1)) / AppState::kGridRows;

    ImVec2 grid_origin = ImGui::GetCursorScreenPos();
    grid_origin.x += side_pad; // Сдвигаем всю сетку вправо

    int start = g_app.page * AppState::kGamesPerPage;
    int end = ((int)g_app.filtered.size() < start + AppState::kGamesPerPage)
        ? (int)g_app.filtered.size()
        : (start + AppState::kGamesPerPage);

    g_app.outline_any_hover = false;
    bool busy = g_app.IsBusy();

    for (int idx = start; idx < end; idx++) {
        int gi = g_app.filtered[idx];
        GameEntry& g = g_app.all_games[gi];

        int local = idx - start;
        int col = local % AppState::kGridCols;
        int row = local / AppState::kGridCols;

        ImVec2 p0 = ImVec2(grid_origin.x + col * (card_w + gap), grid_origin.y + row * (card_h + gap));
        ImVec2 p1 = ImVec2(p0.x + card_w, p0.y + card_h);

        RequestImageLoad(g);

        bool hovered = ImGui::IsMouseHoveringRect(p0, p1) && !ImGui::IsAnyItemActive() && !busy;
        if (hovered) {
            g_app.outline_any_hover = true;
            g_app.outline_pos_target = p0;
            g_app.outline_size_target = ImVec2(card_w, card_h);
        }

        dl->AddRectFilled(p0, p1, ColF(hovered ? t.card_bg_hover : t.card_bg, 1.0f), 10.0f);

        if (g.texture) {
            if (g.open_time > 0.0f) {
                float t2 = MY_CLAMP(((float)ImGui::GetTime() - g.open_time) * 4.0f, 0.0f, 1.0f);
                g.fade_in = EaseOutCubic(t2);
            }
            dl->AddImageRounded((ImTextureID)g.texture, p0, p1,
                ImVec2(0, 0), ImVec2(1, 1), ColF(ImVec4(1, 1, 1, 1), g.fade_in), 10.0f);
        }
        else {
            // Серый фон
            dl->AddRectFilled(p0, p1, IM_COL32(50, 48, 62, 255), 10.0f);

            // Надпись "Нет изображения" по центру
            const char* no_img = "Нет изображения";
            ImVec2 no_img_sz = ImGui::CalcTextSize(no_img);
            dl->AddText(
                ImVec2(p0.x + (card_w - no_img_sz.x) * 0.5f,
                    p0.y + (card_h - no_img_sz.y) * 0.5f),
                IM_COL32(180, 175, 200, 255), no_img);
        }


        if (g.installed) {
            const char* label = "Скачано";
            ImVec2 ts = ImGui::CalcTextSize(label);
            float pad_x = 8.0f, pad_y = 4.0f;
            float bw = ts.x + pad_x * 2;
            float bh = ts.y + pad_y * 2;
            ImVec2 bp0(p0.x + 8, p0.y + 8);
            ImVec2 bp1(bp0.x + bw, bp0.y + bh);
            dl->AddRectFilled(bp0, bp1, IM_COL32(80, 200, 120, 255), bh * 0.5f);
            dl->AddText(ImVec2(bp0.x + pad_x, bp0.y + pad_y), IM_COL32(0, 0, 0, 255), label);
        }

        // ============================================================
        //  Кнопка "Steam" справа снизу — появляется при наведении
        // ============================================================
        bool card_hovered = ImGui::IsMouseHoveringRect(p0, p1) && !busy;

        if (card_hovered) {
            // Размеры кнопки
            const char* btn_label = "Steam";
            ImVec2 lbl_sz = ImGui::CalcTextSize(btn_label);
            float btn_pad_x = 10.0f;
            float btn_pad_y = 5.0f;
            float btn_w = lbl_sz.x + btn_pad_x * 2;
            float btn_h = lbl_sz.y + btn_pad_y * 2;

            // Позиция — правый нижний угол карточки с отступом
            ImVec2 sp1(p1.x - 8, p1.y - 8);
            ImVec2 sp0(sp1.x - btn_w, sp1.y - btn_h);

            // Проверяем, наведена ли мышь на саму кнопку
            ImVec2 mp = ImGui::GetIO().MousePos;
            bool btn_hovered = mp.x >= sp0.x && mp.x <= sp1.x &&
                mp.y >= sp0.y && mp.y <= sp1.y;

            // Фон кнопки — тёмный, при hover — ярче
            ImU32 bg_col = btn_hovered
                ? IM_COL32(30, 130, 220, 255)
                : IM_COL32(20, 90, 160, 230);
            dl->AddRectFilled(sp0, sp1, bg_col, 6.0f);

            // Тонкая обводка
            dl->AddRect(sp0, sp1,
                btn_hovered ? IM_COL32(120, 200, 255, 255)
                : IM_COL32(80, 160, 230, 180),
                6.0f, 0, 1.0f);

            // Текст "Steam"
            dl->AddText(
                ImVec2(sp0.x + btn_pad_x, sp0.y + btn_pad_y),
                IM_COL32(255, 255, 255, 255), btn_label);

            // Клик по кнопке → открыть Steam
            if (btn_hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
                OpenGameInSteam(g.appid);
            }
        }

        ImGui::SetCursorScreenPos(p0);
        ImGui::PushID(gi);
        ImGui::InvisibleButton("card", ImVec2(card_w, card_h));

        bool item_hovered = ImGui::IsItemHovered();
        if (ImGui::IsItemClicked() && !busy) {
            // Проверяем, что клик был НЕ по кнопке Steam
            ImVec2 mp2 = ImGui::GetIO().MousePos;
            bool clicked_steam = false;

            if (card_hovered) {
                const char* btn_label2 = "Steam";
                ImVec2 lbl_sz2 = ImGui::CalcTextSize(btn_label2);
                float bw2 = lbl_sz2.x + 20.0f;
                float bh2 = lbl_sz2.y + 10.0f;
                ImVec2 sp1b(p1.x - 8, p1.y - 8);
                ImVec2 sp0b(sp1b.x - bw2, sp1b.y - bh2);
                clicked_steam = mp2.x >= sp0b.x && mp2.x <= sp1b.x &&
                    mp2.y >= sp0b.y && mp2.y <= sp1b.y;
            }

            if (!clicked_steam) {
                DownloadManifestAsync(g.appid, g.name);
            }
        }

        // Всплывающая плашка под мышкой — рисуется ПОВЕРХ ВСЕГО
        if (item_hovered) {
            ImVec2 mp = ImGui::GetIO().MousePos;
            ImDrawList* fg = ImGui::GetForegroundDrawList();

            ImVec2 name_sz = ImGui::CalcTextSize(g.name.c_str());
            char idbuf[64];
            snprintf(idbuf, sizeof(idbuf), "AppID: %s", g.appid.c_str());
            ImVec2 id_sz = ImGui::CalcTextSize(idbuf);

            float pad_x = 12.0f, pad_y = 10.0f;
            float box_w = (name_sz.x > id_sz.x ? name_sz.x : id_sz.x) + pad_x * 2;
            float box_h = name_sz.y + id_sz.y + pad_y * 2 + 4.0f;

            ImVec2 box_p0(mp.x + 16, mp.y + 16);
            ImVec2 box_p1(box_p0.x + box_w, box_p0.y + box_h);

            ImVec2 screen = ImGui::GetIO().DisplaySize;
            if (box_p1.x > screen.x - 10) box_p0.x = mp.x - box_w - 16;
            if (box_p1.y > screen.y - 10) box_p0.y = mp.y - box_h - 16;
            box_p1 = ImVec2(box_p0.x + box_w, box_p0.y + box_h);

            // Фон — серый, плотный
            fg->AddRectFilled(box_p0, box_p1, IM_COL32(45, 45, 55, 250), 8.0f);
            fg->AddRect(box_p0, box_p1, ColF(t.accent, 0.7f), 8.0f, 0, 1.2f);

            // Название
            fg->AddText(ImVec2(box_p0.x + pad_x, box_p0.y + pad_y),
                IM_COL32(255, 255, 255, 255), g.name.c_str());

            // AppID акцентом
            fg->AddText(ImVec2(box_p0.x + pad_x, box_p0.y + pad_y + name_sz.y + 4.0f),
                ColF(t.accent, 1.0f), idbuf);
        }

        ImGui::PopID();   // ← добавили закрытие PushID

    }   // ← ЗАКРЫВАЕМ ЦИКЛ for

    g_app.outline_alpha = ExpLerp(g_app.outline_alpha, g_app.outline_any_hover ? 1.0f : 0.0f, 10.0f, dt);
    if (g_app.outline_alpha > 0.01f) {
        if (g_app.outline_size_cur.x <= 1.0f) {
            g_app.outline_pos_cur = g_app.outline_pos_target;
            g_app.outline_size_cur = g_app.outline_size_target;
        }
        g_app.outline_pos_cur = ExpLerp2(g_app.outline_pos_cur, g_app.outline_pos_target, 18.0f, dt);
        g_app.outline_size_cur = ExpLerp2(g_app.outline_size_cur, g_app.outline_size_target, 18.0f, dt);

        ImVec2 o0 = g_app.outline_pos_cur;
        ImVec2 o1 = ImVec2(o0.x + g_app.outline_size_cur.x, o0.y + g_app.outline_size_cur.y);

        for (int i = 6; i >= 1; i--) {
            float e = (float)i * 1.6f;
            dl->AddRect(ImVec2(o0.x - e, o0.y - e), ImVec2(o1.x + e, o1.y + e),
                ColF(t.accent, g_app.outline_alpha * (0.06f / i)), 12.0f + e, 0, 2.0f);
        }
        dl->AddRect(o0, o1, ColF(t.accent, g_app.outline_alpha), 12.0f, 0, 2.5f);
    }

    // ---- пагинация (улучшенная, с отступами от краёв) ----
    int total_pages = std::max(1, (int)((g_app.filtered.size() + AppState::kGamesPerPage - 1) / AppState::kGamesPerPage));

    // Отступ снизу — уменьшен, чтобы пагинация была выше
    ImGui::SetCursorScreenPos(ImVec2(grid_origin.x, grid_origin.y + grid_h + 8));

    ImVec2 avail_pager = ImGui::GetContentRegionAvail();

    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(1, 1, 1, 0.06f));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(t.accent.x, t.accent.y, t.accent.z, 0.30f));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(t.accent.x, t.accent.y, t.accent.z, 0.50f));

    ImGui::BeginDisabled(g_app.page <= 0);
    if (ImGui::Button("<< Назад", ImVec2(130, 34))) g_app.page--;
    ImGui::EndDisabled();

    ImGui::SameLine(0, 20);
    {
        ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 6);
        char page_buf[64];
        snprintf(page_buf, sizeof(page_buf), "Страница %d / %d", g_app.page + 1, total_pages);
        ImGui::TextColored(t.text, "%s", page_buf);
    }

    ImGui::SameLine(0, 20);
    ImGui::BeginDisabled(g_app.page >= total_pages - 1);
    if (ImGui::Button("Вперёд >>", ImVec2(130, 34))) g_app.page++;
    ImGui::EndDisabled();

    ImGui::PopStyleColor(3);
}

// ============================================================
//  Страница БИБЛИОТЕКА
// ============================================================
static void DrawLibraryPage() {
    ThemeColors t = GetTheme();
    bool busy = g_app.IsBusy();
    if (busy) ImGui::BeginDisabled();

    // Отступы от краёв
    ImGui::Dummy(ImVec2(0, 4));
    ImGui::Indent(32.0f);

    if (ImGui::Button("Обновить список", ImVec2(180, 34))) {
        RefreshInstalledFiles();
        RefreshInstalledFlags();
        LogPush("Список библиотеки обновлён");
    }
    ImGui::SameLine(0, 12);
    if (ImGui::Button("Открыть папку Lua", ImVec2(180, 34))) OpenLuaFolder();

    ImGui::Unindent(24.0f);

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    if (g_app.installed.empty()) {
        ImGui::TextDisabled("В библиотеке пока нет установленных игр.");
        if (busy) ImGui::EndDisabled();
        return;
    }

    ImGui::Indent(24.0f);
    ImGui::BeginChild("##lib_scroll", ImVec2(0, 0), false);
    for (size_t i = 0; i < g_app.installed.size(); ++i) {
        auto& e = g_app.installed[i];
        ImGui::PushID(e.appid.c_str());

        ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(1, 1, 1, 0.035f));
        ImGui::BeginChild("##row", ImVec2(0, 68), true,
            ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);

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
        if (ImGui::Button("Скачать заново", ImVec2(160, 36)))
            DownloadManifestAsync(e.appid, e.name);
        ImGui::PopStyleColor(3);

        ImGui::SetCursorPos(ImVec2(ImGui::GetWindowWidth() - 170, 16));
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.80f, 0.25f, 0.35f, 0.9f));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.95f, 0.35f, 0.45f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.65f, 0.18f, 0.28f, 1.0f));
        if (ImGui::Button("Удалить", ImVec2(150, 36))) {
            DeleteInstalled(e.appid);
            ImGui::PopStyleColor(3);
            ImGui::EndChild();
            ImGui::PopStyleColor();
            ImGui::PopID();
            break;
        }
        ImGui::PopStyleColor(3);

        ImGui::EndChild();
        ImGui::PopStyleColor();
        ImGui::PopID();
        ImGui::Dummy(ImVec2(0, 6));
    }
    ImGui::EndChild();
    ImGui::Unindent(24.0f);

    if (busy) ImGui::EndDisabled();
}

// ============================================================
//  Страница ИНСТРУМЕНТЫ
// ============================================================
static void DrawThemeButton(const char* label, ThemeMode mode) {
    bool is_active = (g_theme == mode);
    if (is_active) {
        ThemeColors tc = GetTheme();
        ImGui::PushStyleColor(ImGuiCol_Button, tc.accent);
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, tc.accent_hover);
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, tc.accent_active);
    }
    if (ImGui::Button(label, ImVec2(120, 34))) {
        g_theme = mode;
        ApplyStyle();
        SaveSettings();
    }
    if (is_active) ImGui::PopStyleColor(3);
}

static void DrawToolsPage() {
    ThemeColors t = GetTheme();
    bool busy = g_app.IsBusy();
    if (busy) ImGui::BeginDisabled();

    ImGui::Dummy(ImVec2(0, 4));
    ImGui::Indent(24.0f);

    ImGui::TextDisabled("Инструменты установки и управления Steam");
    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    ImGui::PushStyleColor(ImGuiCol_Button, t.accent);
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, t.accent_hover);
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, t.accent_active);
    if (ImGui::Button("Установить OpenSteamTools", ImVec2(280, 40)))
        InstallOpenSteamToolsAsync();
    ImGui::PopStyleColor(3);
    ImGui::SameLine(0, 12);
    if (ImGui::Button("Установить ZIP вручную", ImVec2(280, 40))) {
        OPENFILENAMEA ofn = {};
        char file[MAX_PATH] = {};
        ofn.lStructSize = sizeof(ofn);
        ofn.lpstrFilter = "ZIP-архивы\0*.zip\0Все файлы\0*.*\0";
        ofn.lpstrFile = file;
        ofn.nMaxFile = MAX_PATH;
        ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST;
        if (GetOpenFileNameA(&ofn)) InstallZipFile(file);
    }

    ImGui::Spacing();
    if (ImGui::Button("Закрыть Steam", ImVec2(280, 40))) CloseSteam();
    ImGui::SameLine(0, 12);
    if (ImGui::Button("Перезапустить Steam", ImVec2(280, 40))) RestartSteam();

    ImGui::Spacing();
    if (ImGui::Button("Открыть папку config/lua", ImVec2(280, 40))) OpenLuaFolder();

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();
    ImGui::TextDisabled("Цветовая тема");

    DrawThemeButton("Тёмная", ThemeMode::Dark); ImGui::SameLine();
    DrawThemeButton("Синяя", ThemeMode::Blue); ImGui::SameLine();
    DrawThemeButton("Фиолет", ThemeMode::Purple); ImGui::SameLine();
    DrawThemeButton("Зелёная", ThemeMode::Green); ImGui::SameLine();
    DrawThemeButton("Красная", ThemeMode::Red);

    DrawThemeButton("Оранж", ThemeMode::Orange); ImGui::SameLine();
    DrawThemeButton("Розовая", ThemeMode::Pink); ImGui::SameLine();
    DrawThemeButton("Бирюз", ThemeMode::Teal); ImGui::SameLine();
    DrawThemeButton("Жёлтая", ThemeMode::Yellow); ImGui::SameLine();
    DrawThemeButton("Серая", ThemeMode::Gray);

    DrawThemeButton("Закат", ThemeMode::Sunset); ImGui::SameLine();
    DrawThemeButton("Океан", ThemeMode::Ocean); ImGui::SameLine();
    DrawThemeButton("Лес", ThemeMode::Forest);

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();
    ImGui::TextDisabled("Путь к Steam:");
    ImGui::TextWrapped("%s", g_app_steam_path.empty() ? "(не найден)" : g_app_steam_path.c_str());

    ImGui::Unindent(24.0f);

    if (busy) ImGui::EndDisabled();
}

// ============================================================
//  Страница УВЕДОМЛЕНИЯ
// ============================================================
static void DrawNotificationsPage() {
    ThemeColors t = GetTheme();

    ImGui::Dummy(ImVec2(0, 4));
    ImGui::Indent(24.0f);

    ImGui::PushStyleColor(ImGuiCol_Button, t.accent);
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, t.accent_hover);
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, t.accent_active);
    if (ImGui::Button("Очистить", ImVec2(140, 34))) {
        std::lock_guard<std::mutex> lk(g_app.log_mtx);
        g_app.log.clear();
        g_app.notifications_count = 0;
    }
    ImGui::PopStyleColor(3);
    ImGui::SameLine(0, 12);
    ImGui::TextDisabled("Всего записей: %d", (int)g_app.log.size());

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    bool empty = false;
    {
        std::lock_guard<std::mutex> lk(g_app.log_mtx);
        empty = g_app.log.empty();
    }
    if (empty) {
        ImGui::Dummy(ImVec2(0, 20));
        ImGui::TextDisabled("Пока нет уведомлений.");
        ImGui::Unindent(24.0f);
        return;
    }

    ImGui::BeginChild("##log_scroll", ImVec2(0, 0), false);
    {
        std::lock_guard<std::mutex> lk(g_app.log_mtx);
        int index = 0;
        for (auto& e : g_app.log) {
            ImGui::PushID(index++);

            ImVec4 accent = e.is_error ? ImVec4(0.95f, 0.35f, 0.45f, 1.0f) : t.accent;

            ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(1, 1, 1, 0.035f));
            ImGui::BeginChild("##entry", ImVec2(0, 48), true,
                ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);

            ImDrawList* dl = ImGui::GetWindowDrawList();
            ImVec2 wp = ImGui::GetWindowPos();
            ImVec2 ws = ImGui::GetWindowSize();

            dl->AddRectFilled(
                ImVec2(wp.x + 8, wp.y + 10),
                ImVec2(wp.x + 12, wp.y + ws.y - 10),
                ColF(accent, 1.0f), 2.0f);

            std::time_t tt = std::chrono::system_clock::to_time_t(e.time);
            std::tm tm; localtime_s(&tm, &tt);
            char timebuf[16];
            snprintf(timebuf, sizeof(timebuf), "%02d:%02d:%02d", tm.tm_hour, tm.tm_min, tm.tm_sec);

            ImGui::SetCursorPos(ImVec2(24, 14));
            ImGui::TextColored(accent, "%s", timebuf);

            ImGui::SetCursorPos(ImVec2(110, 14));
            ImGui::TextColored(e.is_error ? ImVec4(0.95f, 0.55f, 0.62f, 1.0f) : t.text, "%s", e.text.c_str());

            ImGui::EndChild();
            ImGui::PopStyleColor();
            ImGui::PopID();
            ImGui::Dummy(ImVec2(0, 6));
        }
    }
    ImGui::EndChild();

    ImGui::Unindent(24.0f);
}

// ============================================================
//  Затемнение + окно прогресса + галочка
// ============================================================
static void DrawOverlayAndProgress() {
    ProgressMode mode = (ProgressMode)g_app.progress_mode.load();
    ThemeColors t = GetTheme();

    float dt = ImGui::GetIO().DeltaTime;
    if (dt <= 0.0f) dt = 0.016f;

    float target = g_app.progress_target.load();
    if (target <= 0.0f) g_app.progress_display = 0.0f;
    else g_app.progress_display += (target - g_app.progress_display) * 0.9f * dt;

    bool want_overlay = (mode != ProgressMode::None) || g_app.success_visible;
    float overlay_target = want_overlay ? 0.6f : 0.0f;
    g_app.overlay_alpha += (overlay_target - g_app.overlay_alpha) * 3.0f * dt;
    if (g_app.overlay_alpha < 0.001f) g_app.overlay_alpha = 0.0f;

    ImGuiIO& io = ImGui::GetIO();
    ImDrawList* fg = ImGui::GetForegroundDrawList();

    if (g_app.overlay_alpha > 0.001f) {
        fg->AddRectFilled(ImVec2(0, 0), io.DisplaySize,
            IM_COL32(0, 0, 0, (int)(g_app.overlay_alpha * 255)));
    }

    if (mode != ProgressMode::None) {
        const float win_w = 560.0f, win_h = 200.0f;
        float wx = (io.DisplaySize.x - win_w) * 0.5f;
        float wy = (io.DisplaySize.y - win_h) * 0.5f;

        fg->AddRectFilled(ImVec2(wx, wy), ImVec2(wx + win_w, wy + win_h),
            IM_COL32(20, 16, 32, 255), 14.0f);
        fg->AddRect(ImVec2(wx, wy), ImVec2(wx + win_w, wy + win_h),
            ColF(t.accent, 0.6f), 14.0f, 0, 1.5f);

        const char* title = "Загрузка манифеста";
        const char* subtitle = "удалит скачанное этой игры";
        ImU32 bar_color = ColF(t.accent, 1.0f);

        if (mode == ProgressMode::Delete) {
            title = "Удаление файлов";
            subtitle = "идёт удаление...";
            bar_color = IM_COL32(217, 89, 110, 255);
        }
        else if (mode == ProgressMode::InstallTools) {
            title = "Установка OpenSteamTools";
            subtitle = "распаковка и копирование файлов...";
            bar_color = IM_COL32(64, 179, 115, 255);
        }

        fg->AddText(ImVec2(wx + 24, wy + 20), ColF(t.accent, 1.0f), title);

        {
            std::lock_guard<std::mutex> lk(g_app.status_mtx);
            fg->AddText(ImVec2(wx + 24, wy + 52), IM_COL32(230, 230, 240, 255),
                g_app.current_name.c_str());
        }

        const float bar_x = wx + 24, bar_y = wy + 92;
        const float bar_w = win_w - 48, bar_h = 24;

        fg->AddRectFilled(ImVec2(bar_x, bar_y), ImVec2(bar_x + bar_w, bar_y + bar_h),
            IM_COL32(255, 255, 255, 20), 12.0f);

        float prog = MY_CLAMP(g_app.progress_display, 0.0f, 1.0f);
        fg->AddRectFilled(ImVec2(bar_x, bar_y), ImVec2(bar_x + bar_w * prog, bar_y + bar_h),
            bar_color, 12.0f);

        char buf[48];
        if (mode == ProgressMode::Delete)
            snprintf(buf, sizeof(buf), "Удаление... %.0f%%", prog * 100.0f);
        else
            snprintf(buf, sizeof(buf), "%.0f%%", prog * 100.0f);

        ImVec2 tsz = ImGui::CalcTextSize(buf);
        fg->AddText(ImVec2(bar_x + (bar_w - tsz.x) * 0.5f, bar_y + (bar_h - tsz.y) * 0.5f),
            IM_COL32(255, 255, 255, 255), buf);

        fg->AddText(ImVec2(wx + 24, wy + 140), IM_COL32(160, 160, 180, 255), subtitle);

        if (mode == ProgressMode::Download) {
            float btn_x = wx + 24, btn_y = wy + 160, btn_w = 120, btn_h = 30;
            ImVec2 mp = io.MousePos;
            bool hovered = mp.x >= btn_x && mp.x <= btn_x + btn_w && mp.y >= btn_y && mp.y <= btn_y + btn_h;
            ImU32 btn_col = hovered ? IM_COL32(230, 90, 90, 255) : IM_COL32(190, 65, 65, 255);
            fg->AddRectFilled(ImVec2(btn_x, btn_y), ImVec2(btn_x + btn_w, btn_y + btn_h), btn_col, 8.0f);
            const char* cancel_txt = "Отмена";
            ImVec2 ctsz = ImGui::CalcTextSize(cancel_txt);
            fg->AddText(ImVec2(btn_x + (btn_w - ctsz.x) * 0.5f, btn_y + (btn_h - ctsz.y) * 0.5f),
                IM_COL32(255, 255, 255, 255), cancel_txt);
            if (hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) CancelCurrentDownload();
        }
        else if (mode == ProgressMode::Delete) {
            float btn_x = wx + 24, btn_y = wy + 160, btn_w = 120, btn_h = 30;
            fg->AddRectFilled(ImVec2(btn_x, btn_y), ImVec2(btn_x + btn_w, btn_y + btn_h),
                IM_COL32(120, 60, 60, 180), 8.0f);
            const char* cancel_txt = "Отмена";
            ImVec2 ctsz = ImGui::CalcTextSize(cancel_txt);
            fg->AddText(ImVec2(btn_x + (btn_w - ctsz.x) * 0.5f, btn_y + (btn_h - ctsz.y) * 0.5f),
                IM_COL32(200, 200, 200, 180), cancel_txt);
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
            float cx = io.DisplaySize.x * 0.5f, cy = io.DisplaySize.y * 0.5f;
            float a = g_app.success_alpha;
            fg->AddCircleFilled(ImVec2(cx, cy), 90.0f, ColF(t.accent, 0.7f * a));
            fg->AddCircleFilled(ImVec2(cx, cy), 72.0f, ColF(t.accent, a));
            const float thick = 10.0f;
            ImVec2 A(cx - 28.0f, cy + 2.0f), B(cx - 6.0f, cy + 24.0f), C(cx + 30.0f, cy - 20.0f);
            fg->AddLine(A, B, IM_COL32(255, 255, 255, (int)(255 * a)), thick);
            fg->AddLine(B, C, IM_COL32(255, 255, 255, (int)(255 * a)), thick);
            fg->AddCircleFilled(A, thick * 0.5f, IM_COL32(255, 255, 255, (int)(255 * a)));
            fg->AddCircleFilled(B, thick * 0.5f, IM_COL32(255, 255, 255, (int)(255 * a)));
            fg->AddCircleFilled(C, thick * 0.5f, IM_COL32(255, 255, 255, (int)(255 * a)));
        }
    }
}

// ============================================================
//  Основной кадр
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

    {
        ImVec2 origin = ImGui::GetWindowPos();
        ImVec2 size = ImGui::GetWindowSize();
        ImDrawList* dl = ImGui::GetWindowDrawList();
        float dt = ImGui::GetIO().DeltaTime; if (dt <= 0.0f) dt = 0.016f;
        g_app.time_accum += dt;
        DrawAuroraBackground(dl, origin, size, g_app.time_accum);
    }

    g_app.app_open_anim = ExpLerp(g_app.app_open_anim, 1.0f, 6.0f, ImGui::GetIO().DeltaTime);
    ImGui::PushStyleVar(ImGuiStyleVar_Alpha, EaseOutCubic(MY_CLAMP(g_app.app_open_anim, 0.0f, 1.0f)));

    DrawTopBar();

    ImGui::BeginChild("##content", ImVec2(0, 0), false, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(40, 24));
    ImGui::BeginChild("##content_pad", ImVec2(0, 0), false, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);

    switch (g_app.current_tab) {
    case 0: DrawShopPage(); break;
    case 1: DrawLibraryPage(); break;
    case 2: DrawToolsPage(); break;
    case 3: DrawNotificationsPage(); break;
    }

    ImGui::EndChild();
    ImGui::PopStyleVar();
    ImGui::EndChild();
    ImGui::PopStyleVar();

    ImGui::End();

    DrawOverlayAndProgress();
    ProcessPendingTextures();
}

// ============================================================
//  Win32 entry
// ============================================================
int APIENTRY wWinMain(HINSTANCE hInstance, HINSTANCE, LPWSTR, int)
{
    WNDCLASSEXW wc = { sizeof(wc), CS_CLASSDC, WndProc, 0L, 0L,
                       GetModuleHandle(nullptr), nullptr, nullptr, nullptr,
                       nullptr, L"SteamLuaToolsUI", nullptr };
    ::RegisterClassExW(&wc);

    HWND hwnd = ::CreateWindowW(wc.lpszClassName, L"SteamLuaTools 9.1",
        WS_POPUP | WS_MINIMIZEBOX, 100, 100, 1280, 800,
        nullptr, nullptr, wc.hInstance, nullptr);
    g_hwnd = hwnd;

    ApplyAppIcon(hwnd);
    BOOL dark = TRUE;
    DwmSetWindowAttribute(hwnd, 20, &dark, sizeof(dark));

    if (!CreateDeviceD3D(hwnd)) { CleanupDeviceD3D(); ::UnregisterClassW(wc.lpszClassName, wc.hInstance); return 1; }

    ::ShowWindow(hwnd, SW_SHOWDEFAULT);
    ::UpdateWindow(hwnd);

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    io.IniFilename = nullptr;

    LoadFonts();
    LoadSettings();
    ApplyStyle();

    ImGui_ImplWin32_Init(hwnd);
    ImGui_ImplDX11_Init(g_pd3dDevice, g_pd3dDeviceContext);

    g_app_steam_path = FindSteamPath();
    g_app.steam_found = !g_app_steam_path.empty();
    if (g_app.steam_found) LogPush("Steam найден: " + g_app_steam_path);
    else LogPush("Steam не найден в системе", true);

    LoadGamesJson();
    RefreshInstalledFlags();
    RefreshInstalledFiles();
    RebuildFiltered();

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

// ============================================================
//  WndProc
// ============================================================
extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND, UINT, WPARAM, LPARAM);

static LRESULT WINAPI WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    if (ImGui_ImplWin32_WndProcHandler(hWnd, msg, wParam, lParam)) return true;
    switch (msg) {
    case WM_NCCALCSIZE: if (wParam == TRUE) return 0; break;
    case WM_SIZE:
        if (wParam == SIZE_MINIMIZED) return 0;
        g_ResizeWidth = (UINT)LOWORD(lParam); g_ResizeHeight = (UINT)HIWORD(lParam);
        return 0;
    case WM_SYSCOMMAND: if ((wParam & 0xfff0) == SC_KEYMENU) return 0; break;
    case WM_DESTROY: ::PostQuitMessage(0); return 0;
    }
    return ::DefWindowProcW(hWnd, msg, wParam, lParam);
}

// ============================================================
//  D3D11 helpers
// ============================================================
static bool CreateDeviceD3D(HWND hWnd) {
    DXGI_SWAP_CHAIN_DESC sd; ZeroMemory(&sd, sizeof(sd));
    sd.BufferCount = 2;
    sd.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    sd.BufferDesc.RefreshRate.Numerator = 60;
    sd.BufferDesc.RefreshRate.Denominator = 1;
    sd.Flags = DXGI_SWAP_CHAIN_FLAG_ALLOW_MODE_SWITCH;
    sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    sd.OutputWindow = hWnd;
    sd.SampleDesc.Count = 1;
    sd.Windowed = TRUE;
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
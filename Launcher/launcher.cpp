// Watermark Injection Launcher - ImGui Edition
#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <shellapi.h>
#include <commctrl.h>
#include <tlhelp32.h>
#include <shlobj.h>
#include <strsafe.h>
#include <string>
#include <vector>
#include <thread>
#include <atomic>
#include <algorithm>
#include <deque>
#include <cstdlib>
#include <GL/GL.h>
#include "imgui.h"
#include "imgui_impl_win32.h"
#include "imgui_impl_opengl3.h"

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

#pragma comment(lib, "comctl32.lib")
#pragma comment(lib, "opengl32.lib")

#define ID_TRAY_SHOW     1001
#define ID_TRAY_INJECT   1002
#define ID_TRAY_EXIT     1003
#define ID_MULTI_DIALOG  2000
#define ID_LIST_MINECRAFT 1201
#define ID_BTN_SEL_INJECT 1202
#define ID_BTN_SEL_CANCEL 1203
#define WM_TRAYICON      (WM_APP + 2)
#define IDR_WATERMARK_DLL 3001

const wchar_t* APP_NAME = L"Watermark Injection";
const wchar_t* APP_TITLE = L"Watermark Injection \u6CE8\u5165\u5668";
const wchar_t* APP_DIR = L"\\WatermarkInjection";
const wchar_t* APP_CLASS = L"WatermarkInjectionLauncher_ImGui";
const wchar_t* INI_FILE = L"config.ini";

struct MinecraftInstance { DWORD pid; std::wstring title; std::wstring processName; };
struct LogEntry { std::wstring text; ImU32 color; };

HINSTANCE g_hInst = NULL;
HWND g_hWnd = NULL;
NOTIFYICONDATAW g_nid = {};
std::wstring g_appDataPath, g_launcherDir;
std::atomic<bool> g_running(true), g_injecting(false), g_autoInject(true);
bool g_autoStart = true, g_hideToTray = true;
int  g_editKey = VK_INSERT;        // edit-mode toggle key written for the DLL
bool g_captureEditKey = false;     // UI is waiting for the next key press
DWORD g_injectTargetPid = 0;
CRITICAL_SECTION g_pidLock;
std::vector<DWORD> g_injectedPids;
std::deque<LogEntry> g_logs;
std::wstring g_statusText = L"就绪";
ImU32 g_statusColor = IM_COL32(100,200,100,255);
bool g_showWindow = true;
std::wstring g_embeddedDllPath;   // DLL extracted from embedded resource
bool g_embeddedDllOk = false;     // extraction succeeded
std::vector<std::string> g_overlayNames; // watermarks found in overlays.json

std::wstring GetLauncherDir() {
    wchar_t p[MAX_PATH]; GetModuleFileNameW(g_hInst, p, MAX_PATH);
    std::wstring full(p);
    size_t pos = full.find_last_of(L"\\/");
    return (pos != std::wstring::npos) ? full.substr(0, pos + 1) : L"";
}
// Defined below (near FindAllMinecraft) - checks the per-process injection
// guard mutex created by the injected DLL.
bool IsProcessInjected(DWORD pid);
std::wstring GetIniPath() {
    if (g_launcherDir.empty()) g_launcherDir = GetLauncherDir();
    return g_launcherDir + INI_FILE;
}
void LoadConfig() {
    std::wstring ini = GetIniPath();
    g_autoInject = (GetPrivateProfileIntW(L"Settings", L"AutoInject", 1, ini.c_str()) != 0);
    g_autoStart = (GetPrivateProfileIntW(L"Settings", L"AutoStart", 1, ini.c_str()) != 0);
    g_hideToTray = (GetPrivateProfileIntW(L"Settings", L"HideToTray", 1, ini.c_str()) != 0);
    int ek = GetPrivateProfileIntW(L"Settings", L"EditKey", VK_INSERT, ini.c_str());
    if (ek >= 1 && ek <= 254) g_editKey = ek;
}
void SaveConfig() {
    std::wstring ini = GetIniPath();
    WritePrivateProfileStringW(L"Settings", L"AutoInject", g_autoInject ? L"1" : L"0", ini.c_str());
    WritePrivateProfileStringW(L"Settings", L"AutoStart", g_autoStart ? L"1" : L"0", ini.c_str());
    WritePrivateProfileStringW(L"Settings", L"HideToTray", g_hideToTray ? L"1" : L"0", ini.c_str());
    wchar_t buf[16]; swprintf_s(buf, L"%d", g_editKey);
    WritePrivateProfileStringW(L"Settings", L"EditKey", buf, ini.c_str());
}

// The injected DLL polls this file (at most every 500 ms) for the edit-mode key.
void WriteEditKeyCfg() {
    wchar_t appData[MAX_PATH];
    if (FAILED(SHGetFolderPathW(NULL, CSIDL_APPDATA, NULL, 0, appData))) return;
    std::wstring dir = std::wstring(appData) + L"\\WatermarkDLL";
    CreateDirectoryW(dir.c_str(), NULL);
    std::wstring path = dir + L"\\edit_key.cfg";
    char content[16];
    sprintf_s(content, "%d\n", g_editKey);  // narrow ASCII: the DLL parses with ifstream >> int
    HANDLE f = CreateFileW(path.c_str(), GENERIC_WRITE, FILE_SHARE_READ, NULL,
                           CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (f == INVALID_HANDLE_VALUE) return;
    DWORD n = (DWORD)strlen(content);
    WriteFile(f, content, n, &n, NULL);
    CloseHandle(f);
}

std::wstring KeyName(int vk) {
    LONG sc = MapVirtualKeyW(vk, MAPVK_VK_TO_VSC) << 16;
    wchar_t buf[64] = {};
    if (GetKeyNameTextW(sc, buf, 64) && buf[0]) return buf;
    return L"?";
}
void InitAppData() {
    wchar_t p[MAX_PATH]; SHGetFolderPathW(NULL, CSIDL_APPDATA, NULL, 0, p);
    g_appDataPath = std::wstring(p) + APP_DIR;
    CreateDirectoryW(g_appDataPath.c_str(), NULL);
    CreateDirectoryW((g_appDataPath + L"\\config").c_str(), NULL);
    CreateDirectoryW((g_appDataPath + L"\\config\\images").c_str(), NULL);
}
void TrayIcon(HWND h, bool add) {
    ZeroMemory(&g_nid, sizeof(g_nid));
    g_nid.cbSize = sizeof(g_nid); g_nid.hWnd = h; g_nid.uID = 1;
    g_nid.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP;
    g_nid.uCallbackMessage = WM_TRAYICON;
    g_nid.hIcon = LoadIcon(NULL, IDI_APPLICATION);
    wcscpy_s(g_nid.szTip, APP_NAME);
    Shell_NotifyIconW(add ? NIM_ADD : NIM_DELETE, &g_nid);
}
void AddLog(ImU32 col, const wchar_t* fmt, ...) {
    wchar_t buf[512]; va_list ap; va_start(ap, fmt);
    StringCchVPrintfW(buf, 512, fmt, ap); va_end(ap);
    wchar_t ts[32]; SYSTEMTIME st; GetLocalTime(&st);
    swprintf_s(ts, L"[%02d:%02d:%02d] ", st.wHour, st.wMinute, st.wSecond);
    g_logs.push_back({ std::wstring(ts) + buf, col });
    if (g_logs.size() > 200) g_logs.pop_front();
}
void SetStatus(const wchar_t* s, ImU32 col) { g_statusText = s; g_statusColor = col; }

// Extract the embedded Watermark DLL resource to %APPDATA%/WatermarkInjection
bool ExtractEmbeddedDll() {
    HRSRC res = FindResourceW(g_hInst, MAKEINTRESOURCEW(IDR_WATERMARK_DLL), (LPCWSTR)RT_RCDATA);
    if (!res) { AddLog(IM_COL32(255,100,100,255), L"未找到内嵌DLL资源"); return false; }
    HGLOBAL mem = LoadResource(g_hInst, res);
    if (!mem) { AddLog(IM_COL32(255,100,100,255), L"加载DLL资源失败"); return false; }
    void* data = LockResource(mem);
    DWORD size = SizeofResource(g_hInst, res);
    if (!data || size == 0) { AddLog(IM_COL32(255,100,100,255), L"DLL资源为空"); return false; }

    std::wstring out = g_appDataPath + L"\\WatermarkInjection.dll";
    HANDLE f = CreateFileW(out.c_str(), GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (f == INVALID_HANDLE_VALUE) {
        DWORD err = GetLastError();
        if (err == ERROR_SHARING_VIOLATION)
            AddLog(IM_COL32(255,160,60,255), L"写入DLL失败: 文件被占用(请先关闭游戏), 将使用目录旁DLL");
        else
            AddLog(IM_COL32(255,100,100,255), L"写入DLL失败: %d", err);
        return false;
    }
    DWORD written = 0;
    BOOL ok = WriteFile(f, data, size, &written, NULL);
    CloseHandle(f);
    if (!ok || written != size) { AddLog(IM_COL32(255,100,100,255), L"DLL写入不完整"); return false; }

    g_embeddedDllPath = out;
    AddLog(IM_COL32(100,255,100,255), L"内嵌DLL已释放 (%u KB)", size / 1024);
    return true;
}

// Scan %APPDATA%/WatermarkDLL/overlays.json for watermark file names
void RefreshOverlayList() {
    g_overlayNames.clear();
    wchar_t appdata[MAX_PATH];
    if (FAILED(SHGetFolderPathW(NULL, CSIDL_APPDATA, NULL, 0, appdata))) return;
    std::wstring cfg = std::wstring(appdata) + L"\\WatermarkDLL\\overlays.json";

    HANDLE f = CreateFileW(cfg.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, 0, NULL);
    if (f == INVALID_HANDLE_VALUE) return;
    DWORD size = GetFileSize(f, NULL);
    if (size == 0 || size > 1024 * 1024) { CloseHandle(f); return; }
    std::string buf(size, '\0');
    DWORD rd = 0;
    ReadFile(f, &buf[0], size, &rd, NULL);
    CloseHandle(f);

    size_t pos = 0;
    while ((pos = buf.find("\"fileName\"", pos)) != std::string::npos) {
        size_t q1 = buf.find('"', pos + 10);
        if (q1 == std::string::npos) break;
        size_t colon = buf.find(':', pos);
        if (colon == std::string::npos || colon > q1) { pos += 10; continue; }
        q1 = buf.find('"', colon);
        if (q1 == std::string::npos) break;
        size_t q2 = buf.find('"', q1 + 1);
        if (q2 == std::string::npos) break;
        g_overlayNames.push_back(buf.substr(q1 + 1, q2 - q1 - 1));
        pos = q2 + 1;
    }
}

// Open the watermark config directory in Explorer
void OpenWatermarkConfigDir() {
    wchar_t appdata[MAX_PATH];
    if (FAILED(SHGetFolderPathW(NULL, CSIDL_APPDATA, NULL, 0, appdata))) return;
    std::wstring dir = std::wstring(appdata) + L"\\WatermarkDLL";
    CreateDirectoryW(dir.c_str(), NULL);
    ShellExecuteW(NULL, L"open", dir.c_str(), NULL, NULL, SW_SHOW);
}

std::vector<MinecraftInstance> FindAllMinecraft() {
    std::vector<MinecraftInstance> result;
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snap == INVALID_HANDLE_VALUE) return result;
    PROCESSENTRY32W pe = { sizeof(pe) };
    if (Process32FirstW(snap, &pe)) do {
        if (_wcsicmp(pe.szExeFile, L"javaw.exe") != 0 && _wcsicmp(pe.szExeFile, L"java.exe") != 0) continue;
        struct E { DWORD pid; HWND hwnd; wchar_t title[256]; } ed = { pe.th32ProcessID, NULL };
        EnumWindows([](HWND h, LPARAM lp) -> BOOL {
            E* e = (E*)lp; DWORD wp; GetWindowThreadProcessId(h, &wp);
            if (wp != e->pid || !IsWindowVisible(h) || !IsWindowEnabled(h)) return TRUE;
            wchar_t t[256], cls[64]; GetWindowTextW(h, t, 256); GetClassNameW(h, cls, 64);
            if (wcsstr(t, L"Minecraft") || wcsstr(t, L"\u6211\u7684\u4E16\u754C") || wcsstr(cls, L"GLFW30") || wcsstr(cls, L"LWJGL"))
            { e->hwnd = h; wcscpy_s(e->title, t); return FALSE; }
            return TRUE;
        }, (LPARAM)&ed);
        if (!ed.hwnd) continue;
        // Skip MCs that already have the DLL injected (by us or any other injector)
        if (IsProcessInjected(pe.th32ProcessID)) continue;
        EnterCriticalSection(&g_pidLock);
        bool skip = (std::find(g_injectedPids.begin(), g_injectedPids.end(), pe.th32ProcessID) != g_injectedPids.end());
        LeaveCriticalSection(&g_pidLock);
        if (!skip) result.push_back({ pe.th32ProcessID, ed.title, pe.szExeFile });
    } while (Process32NextW(snap, &pe));
    CloseHandle(snap); return result;
}

// Injection guard: the injected DLL creates this named mutex on load and
// refuses duplicate loads. Checking it here lets us skip MCs already
// injected by ANY injector (including a second copy of this launcher).
bool IsProcessInjected(DWORD pid) {
    wchar_t name[96];
    swprintf_s(name, L"Local\\WatermarkInjection_Active_%lu", pid);
    HANDLE m = OpenMutexW(SYNCHRONIZE, FALSE, name);
    if (m) { CloseHandle(m); return true; }
    return false;
}

bool InjectPid(DWORD pid) {
    g_injecting = true; SetStatus(L"\u6B63\u5728\u6CE8\u5165...", IM_COL32(255,200,50,255));
    // Re-check immediately before injection (closes the race between two launchers)
    if (IsProcessInjected(pid)) {
        AddLog(IM_COL32(255,200,100,255), L"PID:%d \u5DF2\u88AB\u6CE8\u5165, \u8DF3\u8FC7 (\u53EF\u80FD\u662F\u53E6\u4E00\u4E2A\u6CE8\u5165\u5668\u5DF2\u6CE8\u5165)", pid);
        EnterCriticalSection(&g_pidLock);
        if (std::find(g_injectedPids.begin(), g_injectedPids.end(), pid) == g_injectedPids.end()) g_injectedPids.push_back(pid);
        LeaveCriticalSection(&g_pidLock);
        g_injecting = false; return false;
    }
    HANDLE hp = OpenProcess(PROCESS_ALL_ACCESS, FALSE, pid);
    if (!hp) { AddLog(IM_COL32(255,100,100,255), L"\u6253\u5F00\u8FDB\u7A0B\u5931\u8D25 PID:%d", pid); g_injecting = false; return false; }
    // Prefer DLL extracted from embedded resource; fall back to side-by-side files
    std::wstring dllPath;
    if (g_embeddedDllOk && !g_embeddedDllPath.empty() &&
        GetFileAttributesW(g_embeddedDllPath.c_str()) != INVALID_FILE_ATTRIBUTES) {
        dllPath = g_embeddedDllPath;
    } else {
        if (g_launcherDir.empty()) g_launcherDir = GetLauncherDir();
        dllPath = g_launcherDir + L"WatermarkInjection.dll";
        if (GetFileAttributesW(dllPath.c_str()) == INVALID_FILE_ATTRIBUTES) {
            dllPath = g_launcherDir + L"InfiniteGUI-DLL.dll";
            if (GetFileAttributesW(dllPath.c_str()) == INVALID_FILE_ATTRIBUTES)
                dllPath = g_appDataPath + L"\\WatermarkInjection.dll";
        }
    }
    size_t ps = (dllPath.size() + 1) * 2;
    void* rm = VirtualAllocEx(hp, NULL, ps, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    if (!rm) { CloseHandle(hp); AddLog(IM_COL32(255,100,100,255), L"VirtualAllocEx \u5931\u8D25"); g_injecting = false; return false; }
    WriteProcessMemory(hp, rm, dllPath.c_str(), (DWORD)ps, NULL);
    FARPROC ll = GetProcAddress(GetModuleHandleW(L"kernel32.dll"), "LoadLibraryW");
    HANDLE ht = CreateRemoteThread(hp, NULL, 0, (LPTHREAD_START_ROUTINE)ll, rm, 0, NULL);
    bool ok = false;
    if (ht) { WaitForSingleObject(ht, 10000); CloseHandle(ht); AddLog(IM_COL32(100,255,100,255), L"\u6CE8\u5165\u6210\u529F PID:%d", pid); SetStatus(L"\u5DF2\u6CE8\u5165", IM_COL32(100,200,100,255)); ok = true; }
    else { AddLog(IM_COL32(255,100,100,255), L"\u6CE8\u5165\u5931\u8D25: %d", GetLastError()); SetStatus(L"\u6CE8\u5165\u5931\u8D25", IM_COL32(255,100,100,255)); }
    if (ok) { EnterCriticalSection(&g_pidLock); g_injectedPids.push_back(pid); LeaveCriticalSection(&g_pidLock); }
    VirtualFreeEx(hp, rm, 0, MEM_RELEASE); CloseHandle(hp);
    g_injecting = false; return ok;
}

void DoInject(HWND hDlg, const std::vector<MinecraftInstance>& instances);
void DetectionThread() {
    while (g_running) {
        EnterCriticalSection(&g_pidLock);
        g_injectedPids.erase(std::remove_if(g_injectedPids.begin(), g_injectedPids.end(), [](DWORD pid) { HANDLE p = OpenProcess(SYNCHRONIZE, FALSE, pid); bool dead = (p == NULL); if (p) CloseHandle(p); return dead; }), g_injectedPids.end());
        LeaveCriticalSection(&g_pidLock);
        if (!g_injecting && g_autoInject) {
            auto instances = FindAllMinecraft();
            if (!instances.empty()) {
                AddLog(IM_COL32(150,200,255,255), L"\u68C0\u6D4B\u5230 %d \u4E2A MC", (int)instances.size());
                DoInject(NULL, instances);
                for (int i = 0; g_running && i < 5; i++) Sleep(1000);
            } else Sleep(2000);
        } else Sleep(1000);
    }
}

void SetAutoStart(bool en) {
    g_autoStart = en;
    SaveConfig();

    wchar_t exePath[MAX_PATH];
    GetModuleFileNameW(g_hInst, exePath, MAX_PATH);

    // Registry Run key
    HKEY hk;
    LONG rr = RegOpenKeyExW(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Run", 0, KEY_SET_VALUE | KEY_QUERY_VALUE, &hk);
    if (rr == ERROR_SUCCESS) {
        if (en) {
            std::wstring cmd = L"\"" + std::wstring(exePath) + L"\" /silent";
            RegSetValueExW(hk, APP_NAME, 0, REG_SZ, (BYTE*)cmd.c_str(), (DWORD)((cmd.size() + 1) * 2));
        } else {
            RegDeleteValueW(hk, APP_NAME);
        }
        RegCloseKey(hk);
    }

    // Task Scheduler fallback
    char taskCmdBuf[1024];
    if (en)
        sprintf_s(taskCmdBuf, "schtasks /create /tn \"WatermarkInjection\" /f /tr \"\\\"%S\\\" /silent\" /sc onlogon /rl highest /delay 0000:10 2>nul", exePath);
    else
        sprintf_s(taskCmdBuf, "schtasks /delete /tn \"WatermarkInjection\" /f 2>nul");
    system(taskCmdBuf);

    AddLog(en ? IM_COL32(100,255,100,255) : IM_COL32(255,200,100,255),
        en ? L"\u5F00\u673A\u542F\u52A8\u5DF2\u542F\u7528" : L"\u5F00\u673A\u542F\u52A8\u5DF2\u5173\u95ED");
}

INT_PTR CALLBACK MultiDialogProc(HWND hDlg, UINT msg, WPARAM w, LPARAM l);
void DoInject(HWND, const std::vector<MinecraftInstance>& instances) {
    if (instances.empty()) return;
    if (instances.size() == 1) { InjectPid(instances[0].pid); return; }
    g_injectTargetPid = 0;
    DialogBoxParamW(g_hInst, MAKEINTRESOURCEW(ID_MULTI_DIALOG), g_hWnd, MultiDialogProc, (LPARAM)&instances);
    if (g_injectTargetPid) InjectPid(g_injectTargetPid);
}
INT_PTR CALLBACK MultiDialogProc(HWND hDlg, UINT msg, WPARAM w, LPARAM l) {
    switch (msg) {
    case WM_INITDIALOG: {
        auto* instances = (std::vector<MinecraftInstance>*)l;
        HWND hList = GetDlgItem(hDlg, ID_LIST_MINECRAFT);
        for (size_t i = 0; i < instances->size(); i++) {
            wchar_t buf[512]; swprintf_s(buf, L"PID: %d | %s | %s", (*instances)[i].pid, (*instances)[i].processName.c_str(), (*instances)[i].title.c_str());
            SendMessageW(hList, LB_ADDSTRING, 0, (LPARAM)buf); SendMessageW(hList, LB_SETITEMDATA, i, (LPARAM)(*instances)[i].pid);
        }
        SendMessageW(hList, LB_SETCURSEL, 0, 0); return TRUE;
    }
    case WM_COMMAND:
        if (LOWORD(w) == ID_BTN_SEL_INJECT) { HWND hList = GetDlgItem(hDlg, ID_LIST_MINECRAFT); int sel = (int)SendMessageW(hList, LB_GETCURSEL, 0, 0); if (sel != LB_ERR) g_injectTargetPid = (DWORD)SendMessageW(hList, LB_GETITEMDATA, sel, 0); EndDialog(hDlg, IDOK); }
        if (LOWORD(w) == ID_BTN_SEL_CANCEL) EndDialog(hDlg, IDCANCEL);
        return TRUE;
    }
    return FALSE;
}

static bool s_firstFrame = true;
static std::string W2UTF8(const std::wstring& ws) {
    int len = WideCharToMultiByte(CP_UTF8, 0, ws.c_str(), -1, NULL, 0, NULL, NULL);
    std::string s(len - 1, 0);
    WideCharToMultiByte(CP_UTF8, 0, ws.c_str(), -1, &s[0], len, NULL, NULL);
    return s;
}

// Modern dark theme with blue accent
void ApplyStyle() {
    ImGuiStyle& s = ImGui::GetStyle();
    s.WindowRounding = 10.0f;
    s.FrameRounding = 6.0f;
    s.ChildRounding = 8.0f;
    s.PopupRounding = 6.0f;
    s.ScrollbarRounding = 6.0f;
    s.GrabRounding = 4.0f;
    s.WindowBorderSize = 0.0f;
    s.FrameBorderSize = 0.0f;
    s.ItemSpacing = ImVec2(8, 6);
    s.WindowPadding = ImVec2(12, 12);

    ImVec4* c = s.Colors;
    c[ImGuiCol_WindowBg] = ImVec4(0.051f, 0.051f, 0.075f, 1.00f);
    c[ImGuiCol_ChildBg] = ImVec4(0.086f, 0.086f, 0.118f, 1.00f);
    c[ImGuiCol_FrameBg] = ImVec4(0.118f, 0.118f, 0.157f, 1.00f);
    c[ImGuiCol_FrameBgHovered] = ImVec4(0.157f, 0.157f, 0.208f, 1.00f);
    c[ImGuiCol_FrameBgActive] = ImVec4(0.196f, 0.196f, 0.255f, 1.00f);
    c[ImGuiCol_Button] = ImVec4(0.118f, 0.118f, 0.157f, 1.00f);
    c[ImGuiCol_ButtonHovered] = ImVec4(0.180f, 0.180f, 0.235f, 1.00f);
    c[ImGuiCol_ButtonActive] = ImVec4(0.240f, 0.240f, 0.310f, 1.00f);
    c[ImGuiCol_Header] = ImVec4(0.24f, 0.42f, 0.75f, 0.55f);
    c[ImGuiCol_HeaderHovered] = ImVec4(0.31f, 0.50f, 0.85f, 0.65f);
    c[ImGuiCol_CheckMark] = ImVec4(0.31f, 0.55f, 1.00f, 1.00f);
    c[ImGuiCol_SliderGrab] = ImVec4(0.31f, 0.55f, 1.00f, 1.00f);
    c[ImGuiCol_Text] = ImVec4(0.92f, 0.92f, 0.95f, 1.00f);
    c[ImGuiCol_TextDisabled] = ImVec4(0.50f, 0.50f, 0.55f, 1.00f);
    c[ImGuiCol_ScrollbarBg] = ImVec4(0.05f, 0.05f, 0.07f, 0.0f);
    c[ImGuiCol_ScrollbarGrab] = ImVec4(0.20f, 0.20f, 0.27f, 1.00f);
    c[ImGuiCol_ScrollbarGrabHovered] = ImVec4(0.28f, 0.28f, 0.36f, 1.00f);
    c[ImGuiCol_Separator] = ImVec4(0.20f, 0.20f, 0.28f, 0.60f);
}

// Throttled Minecraft detection (EnumWindows every 1s)
static int s_mcCount = 0;
static DWORD s_lastMcCheck = 0;
int GetMcCountThrottled() {
    DWORD now = GetTickCount();
    if (now - s_lastMcCheck > 1000) {
        s_lastMcCheck = now;
        s_mcCount = (int)FindAllMinecraft().size();
    }
    return s_mcCount;
}

void RenderUI() {
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
    ImGui::SetNextWindowPos(ImVec2(0, 0));
    ImGui::SetNextWindowSize(ImGui::GetIO().DisplaySize);

    ImGuiWindowFlags wf = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoBringToFrontOnFocus;
    ImGui::Begin("##Main", nullptr, wf);
    ImDrawList* dl = ImGui::GetWindowDrawList();

    const float w = ImGui::GetWindowWidth();
    const float h = ImGui::GetWindowHeight();
    const float pad = 12.0f;

    // ---------- Header ----------
    dl->AddRectFilledMultiColor(ImVec2(0, 0), ImVec2(w, 48),
        IM_COL32(32, 38, 66, 255), IM_COL32(20, 22, 40, 255),
        IM_COL32(20, 22, 40, 255), IM_COL32(32, 38, 66, 255));
    dl->AddRectFilled(ImVec2(0, 48), ImVec2(w, 49), IM_COL32(79, 140, 255, 160));
    dl->AddCircleFilled(ImVec2(22, 24), 6, IM_COL32(79, 140, 255, 255));
    dl->AddCircleFilled(ImVec2(22, 24), 3, IM_COL32(160, 195, 255, 255));

    ImGui::SetCursorPos(ImVec2(38, 8));
    ImGui::TextColored(ImVec4(1, 1, 1, 0.95f), "%s", W2UTF8(APP_TITLE).c_str());
    ImGui::SetCursorPos(ImVec2(38, 27));
    ImGui::TextColored(ImVec4(0.62f, 0.68f, 0.82f, 1.0f), "Minecraft 水印注入工具");

    // ---------- Status cards ----------
    float cardY = 60, cardH = 64;
    float cardW = (w - pad * 2 - 8 * 2) / 3.0f;
    auto drawCard = [&](const char* id, float x, const char* label) {
        ImGui::SetCursorPos(ImVec2(x, cardY));
        ImGui::PushStyleColor(ImGuiCol_ChildBg, IM_COL32(22, 22, 30, 255));
        ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 8);
        ImGui::BeginChild(id, ImVec2(cardW, cardH), false);
        ImGui::SetCursorPos(ImVec2(12, 9));
        ImGui::TextColored(ImVec4(0.55f, 0.58f, 0.66f, 1), "%s", label);
        ImGui::EndChild();
        ImGui::PopStyleVar(); ImGui::PopStyleColor();
    };

    drawCard("CardStatus", pad, "注入状态");
    {
        ImGui::SetCursorPos(ImVec2(pad + 12, cardY + 30));
        ImU32 sc = g_statusColor;
        ImVec2 stPos = ImGui::GetCursorScreenPos();
        dl->AddCircleFilled(ImVec2(stPos.x + 6, stPos.y + 9), 5, sc);
        ImGui::SetCursorPos(ImVec2(pad + 32, cardY + 30));
        std::string st = W2UTF8(g_statusText);
        ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(sc), "%s", st.c_str());
    }

    drawCard("CardMC", pad + cardW + 8, "Minecraft 检测");
    {
        int mc = GetMcCountThrottled();
        ImGui::SetCursorPos(ImVec2(pad + cardW + 8 + 12, cardY + 30));
        if (mc == 0) ImGui::TextColored(ImVec4(0.50f, 0.52f, 0.58f, 1), "未检测到");
        else ImGui::TextColored(ImVec4(0.42f, 1.0f, 0.55f, 1), "发现 %d 个进程", mc);
    }

    drawCard("CardInj", pad + (cardW + 8) * 2, "已注入");
    {
        EnterCriticalSection(&g_pidLock);
        int inj = (int)g_injectedPids.size();
        LeaveCriticalSection(&g_pidLock);
        ImGui::SetCursorPos(ImVec2(pad + (cardW + 8) * 2 + 12, cardY + 30));
        if (inj == 0) ImGui::TextColored(ImVec4(0.50f, 0.52f, 0.58f, 1), "暂无");
        else ImGui::TextColored(ImVec4(0.42f, 1.0f, 0.55f, 1), "%d 个进程", inj);
    }

    // ---------- Watermark management ----------
    float wmY = cardY + cardH + 10;
    float wmH = 128;
    ImGui::SetCursorPos(ImVec2(pad, wmY));
    ImGui::PushStyleColor(ImGuiCol_ChildBg, IM_COL32(22, 22, 30, 255));
    ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 8);
    ImGui::BeginChild("WatermarkCard", ImVec2(w - pad * 2, wmH), false);
    ImGui::SetCursorPos(ImVec2(12, 8));
    ImGui::TextColored(ImVec4(0.55f, 0.58f, 0.66f, 1), "水印管理");
    ImGui::SameLine();
    ImGui::TextColored(ImVec4(0.31f, 0.55f, 1.0f, 1), "%d", (int)g_overlayNames.size());
    ImGui::SameLine();
    float btnX = w - pad * 2 - 12 - 168;
    if (btnX > ImGui::GetCursorPosX() + 60) ImGui::SameLine(btnX);
    if (ImGui::SmallButton("刷新")) RefreshOverlayList();
    ImGui::SameLine();
    if (ImGui::SmallButton("打开配置目录")) OpenWatermarkConfigDir();
    ImGui::SetCursorPos(ImVec2(12, 30));
    ImGui::PushStyleColor(ImGuiCol_Separator, IM_COL32(40, 40, 54, 255));
    ImGui::Separator();
    ImGui::PopStyleColor();

    ImGui::SetCursorPos(ImVec2(12, 40));
    if (g_overlayNames.empty()) {
        ImGui::TextColored(ImVec4(0.45f, 0.47f, 0.53f, 1), "暂无水印 — 进入游戏后拖放图片到窗口即可添加");
    } else {
        int shown = 0;
        for (auto& name : g_overlayNames) {
            if (shown >= 3) break;
            ImGui::SetCursorPos(ImVec2(14, 40 + shown * 20));
            // GetCursorScreenPos: the child window has a non-zero client offset,
            // so raw (14,40+...) here would draw dots over the status cards.
            ImVec2 rowPos = ImGui::GetCursorScreenPos();
            dl->AddCircleFilled(ImVec2(rowPos.x + 8, rowPos.y + 9), 3, IM_COL32(79, 140, 255, 255));
            ImGui::SetCursorPos(ImVec2(28, 40 + shown * 20));
            std::string disp = name.length() > 46 ? name.substr(0, 46) + "..." : name;
            ImGui::TextColored(ImVec4(0.80f, 0.83f, 0.90f, 1), "%s", disp.c_str());
            shown++;
        }
        int extra = (int)g_overlayNames.size() - shown;
        if (extra > 0) {
            ImGui::SetCursorPos(ImVec2(28, 40 + shown * 20));
            ImGui::TextColored(ImVec4(0.45f, 0.47f, 0.53f, 1), "... 以及另外 %d 个", extra);
        }
    }
    ImGui::EndChild();
    ImGui::PopStyleVar(); ImGui::PopStyleColor();

    // ---------- Action buttons ----------
    float btnY = wmY + wmH + 10;
    ImGui::SetCursorPos(ImVec2(pad, btnY));
    ImGui::PushStyleColor(ImGuiCol_Button, IM_COL32(46, 94, 168, 255));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, IM_COL32(58, 114, 200, 255));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, IM_COL32(72, 132, 224, 255));
    if (ImGui::Button("手动注入", ImVec2(130, 34))) {
        auto inst = FindAllMinecraft(); DoInject(g_hWnd, inst);
    }
    ImGui::PopStyleColor(3);
    ImGui::SameLine();
    if (ImGui::Button("隐藏到托盘", ImVec2(110, 34))) { g_showWindow = false; ShowWindow(g_hWnd, SW_HIDE); }

    // ---------- Options ----------
    float optY = btnY + 42;
    ImGui::SetCursorPos(ImVec2(pad + 2, optY));
    bool tmpAutoInject = g_autoInject.load();
    if (ImGui::Checkbox("自动注入", &tmpAutoInject)) { g_autoInject = tmpAutoInject; SaveConfig(); }
    ImGui::SameLine();
    if (ImGui::Checkbox("开机启动", &g_autoStart)) SetAutoStart(g_autoStart);
    ImGui::SameLine();
    if (ImGui::Checkbox("关闭时隐藏托盘", &g_hideToTray)) SaveConfig();

    // Edit-mode toggle key (consumed by the injected DLL)
    ImGui::SetCursorPos(ImVec2(pad + 2, optY + 26));
    ImGui::TextColored(ImVec4(0.55f, 0.58f, 0.66f, 1), "编辑模式按键:");
    ImGui::SameLine();
    if (g_captureEditKey) {
        ImGui::TextColored(ImVec4(1.0f, 0.82f, 0.35f, 1), "请按下新按键... (Esc 取消)");
    } else {
        std::string kn = W2UTF8(KeyName(g_editKey));
        ImGui::TextColored(ImVec4(0.42f, 1.0f, 0.55f, 1), "%s", kn.c_str());
        ImGui::SameLine();
        if (ImGui::SmallButton("修改")) g_captureEditKey = true;
    }

    // ---------- Log ----------
    float logY = optY + 58;
    ImGui::SetCursorPos(ImVec2(pad, logY));
    ImGui::PushStyleColor(ImGuiCol_ChildBg, IM_COL32(14, 14, 20, 255));
    ImGui::BeginChild("Log", ImVec2(w - pad * 2, h - logY - 34), false);
    ImGui::SetCursorPos(ImVec2(8, 6));
    for (auto& e : g_logs) {
        char utf8[512]; WideCharToMultiByte(CP_UTF8, 0, e.text.c_str(), -1, utf8, 512, NULL, NULL);
        ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(e.color), "%s", utf8);
    }
    if (ImGui::GetScrollY() >= ImGui::GetScrollMaxY()) ImGui::SetScrollHereY(1.0f);
    ImGui::EndChild();
    ImGui::PopStyleColor();

    // ---------- Footer ----------
    ImGui::SetCursorPos(ImVec2(pad + 2, h - 26));
    ImGui::TextColored(ImVec4(0.42f, 0.44f, 0.50f, 1), "v1.1.0");
    ImGui::SameLine();
    if (g_embeddedDllOk) ImGui::TextColored(ImVec4(0.35f, 0.65f, 0.45f, 1), "  |  DLL 已内嵌");
    else ImGui::TextColored(ImVec4(0.80f, 0.45f, 0.35f, 1), "  |  DLL 内嵌失败");

    ImGui::End();
    ImGui::PopStyleVar();
}

LRESULT CALLBACK WndProc(HWND hWnd, UINT msg, WPARAM w, LPARAM l) {
    // Edit-key capture has priority over everything (including ImGui)
    if (g_captureEditKey && msg == WM_KEYDOWN) {
        g_captureEditKey = false;
        if (w != VK_ESCAPE) {
            g_editKey = (int)w;
            SaveConfig();
            WriteEditKeyCfg();
            AddLog(IM_COL32(100, 200, 100, 255), L"编辑模式按键已改为 %s", KeyName(g_editKey).c_str());
        }
        return 0;
    }
    if (ImGui_ImplWin32_WndProcHandler(hWnd, msg, w, l)) return 1;

    if (msg == WM_TRAYICON) {
        if (l == WM_LBUTTONDBLCLK) { g_showWindow = true; ShowWindow(hWnd, SW_SHOW); SetForegroundWindow(hWnd); }
        if (l == WM_RBUTTONUP) {
            HMENU m = CreatePopupMenu();
            AppendMenuW(m, MF_STRING, ID_TRAY_SHOW, L"\u663E\u793A\u7A97\u53E3");
            AppendMenuW(m, MF_STRING, ID_TRAY_INJECT, L"\u624B\u52A8\u6CE8\u5165");
            AppendMenuW(m, MF_SEPARATOR, 0, NULL);
            AppendMenuW(m, MF_STRING, ID_TRAY_EXIT, L"\u9000\u51FA");
            SetForegroundWindow(hWnd); POINT pt; GetCursorPos(&pt);
            TrackPopupMenu(m, TPM_RIGHTBUTTON, pt.x, pt.y, 0, hWnd, NULL); DestroyMenu(m);
        }
        return 0;
    }
    if (msg == WM_COMMAND) {
        switch (LOWORD(w)) {
        case ID_TRAY_SHOW: g_showWindow = true; ShowWindow(hWnd, SW_SHOW); SetForegroundWindow(hWnd); return 0;
        case ID_TRAY_INJECT: { auto inst = FindAllMinecraft(); DoInject(hWnd, inst); return 0; }
        case ID_TRAY_EXIT: g_running = false; DestroyWindow(hWnd); return 0;
        }
    }
    if (msg == WM_CLOSE) {
        if (g_hideToTray) { g_showWindow = false; ShowWindow(hWnd, SW_HIDE); }
        else DestroyWindow(hWnd);
        return 0;
    }
    if (msg == WM_DESTROY) {
        g_running = false; TrayIcon(hWnd, false); DeleteCriticalSection(&g_pidLock);
        ImGui_ImplOpenGL3_Shutdown(); ImGui_ImplWin32_Shutdown(); ImGui::DestroyContext();
        PostQuitMessage(0); return 0;
    }
    if (msg == WM_SIZE) {
        RECT r; GetClientRect(hWnd, &r);
        if (r.right > 0 && r.bottom > 0) { /* viewport handled in render */ }
        return 0;
    }
    return DefWindowProcW(hWnd, msg, w, l);
}

int WINAPI wWinMain(HINSTANCE hInst, HINSTANCE, PWSTR cmd, int) {
    g_hInst = hInst; g_launcherDir = GetLauncherDir();
    // Single instance: a second copy activates the existing window and exits,
    // so two launchers can never race to inject the same Minecraft.
    HANDLE single = CreateMutexW(NULL, TRUE, L"Local\\WatermarkInjectionLauncher_SingleInstance");
    if (!single || GetLastError() == ERROR_ALREADY_EXISTS) {
        HWND existing = FindWindowW(APP_CLASS, NULL);
        if (existing) {
            if (!IsWindowVisible(existing)) ShowWindow(existing, SW_SHOW);
            if (IsIconic(existing)) ShowWindow(existing, SW_RESTORE);
            SetForegroundWindow(existing);
        }
        return 0;
    }
    InitializeCriticalSection(&g_pidLock);
    LoadConfig();
    WriteEditKeyCfg();  // make sure the DLL sees the configured edit-mode key
    InitCommonControls();
    InitAppData();
    g_embeddedDllOk = ExtractEmbeddedDll();
    RefreshOverlayList();
    if (g_autoStart) SetAutoStart(g_autoStart);

    WNDCLASSEXW wc = { sizeof(WNDCLASSEXW), CS_OWNDC, WndProc, 0, 0, hInst, LoadIcon(NULL, IDI_APPLICATION), LoadCursor(NULL, IDC_ARROW), NULL, NULL, APP_CLASS, NULL };
    RegisterClassExW(&wc);

    bool silent = (wcsstr(cmd, L"/silent") != NULL);
    RECT wr = { 0, 0, 580, 560 }; AdjustWindowRect(&wr, WS_OVERLAPPEDWINDOW, FALSE);
    HWND hWnd = CreateWindowExW(WS_EX_APPWINDOW, APP_CLASS, APP_TITLE, WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX, CW_USEDEFAULT, CW_USEDEFAULT, wr.right - wr.left, wr.bottom - wr.top, NULL, NULL, hInst, NULL);
    if (!hWnd) return 1;
    g_hWnd = hWnd;

    HDC hdc = GetDC(hWnd);
    PIXELFORMATDESCRIPTOR pfd = { sizeof(pfd), 1, PFD_DRAW_TO_WINDOW | PFD_SUPPORT_OPENGL | PFD_DOUBLEBUFFER, PFD_TYPE_RGBA, 32, 0,0,0,0,0,0, 0,0,0, 0,0,0,0, 24,8, 0, PFD_MAIN_PLANE, 0, 0,0,0 };
    SetPixelFormat(hdc, ChoosePixelFormat(hdc, &pfd), &pfd);
    HGLRC glrc = wglCreateContext(hdc); wglMakeCurrent(hdc, glrc);
    ReleaseDC(hWnd, hdc);

    IMGUI_CHECKVERSION(); ImGui::CreateContext(); ImGui::GetIO().IniFilename = nullptr;
    ImGui::StyleColorsDark();
    ApplyStyle();

    ImGuiIO& io = ImGui::GetIO();
    ImFontConfig cfg; cfg.FontDataOwnedByAtlas = false;
    io.Fonts->AddFontFromFileTTF("C:\\Windows\\Fonts\\msyh.ttc", 18.0f, &cfg, io.Fonts->GetGlyphRangesChineseFull());

    ImGui_ImplWin32_Init(hWnd);
    ImGui_ImplOpenGL3_Init("#version 130");

    TrayIcon(hWnd, true);
    AddLog(IM_COL32(100,200,100,255), L"\u542F\u52A8\u5668\u5DF2\u542F\u52A8");
    std::thread(DetectionThread).detach();

    g_showWindow = !silent;
    ShowWindow(hWnd, silent ? SW_HIDE : SW_SHOW);

    MSG msg = {};
    while (msg.message != WM_QUIT) {
        while (PeekMessage(&msg, NULL, 0, 0, PM_REMOVE)) {
            TranslateMessage(&msg); DispatchMessage(&msg);
            if (msg.message == WM_QUIT) break;
        }
        if (msg.message == WM_QUIT) break;

        ImGui_ImplOpenGL3_NewFrame(); ImGui_ImplWin32_NewFrame(); ImGui::NewFrame();

        if (g_showWindow) {
            RenderUI();
            ImGui::Render();
            RECT r; GetClientRect(hWnd, &r);
            glViewport(0, 0, r.right, r.bottom);
            glClearColor(0.08f, 0.08f, 0.12f, 1.0f);
            glClear(GL_COLOR_BUFFER_BIT);
            ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
            SwapBuffers(GetDC(hWnd));
        } else {
            ImGui::Render();
            Sleep(100);
        }

        if (s_firstFrame) {
            AddLog(IM_COL32(150,150,150,255), L"\u6570\u636E\u76EE\u5F55: %s", g_appDataPath.c_str());
            s_firstFrame = false;
        }

        // Periodically refresh watermark list (game may add/remove overlays)
        static DWORD s_lastOvRefresh = 0;
        if (GetTickCount() - s_lastOvRefresh > 5000) {
            s_lastOvRefresh = GetTickCount();
            RefreshOverlayList();
        }
    }

    wglMakeCurrent(NULL, NULL); wglDeleteContext(glrc);
    return 0;
}

// Haste2 Taskbar Battery - transparent popup aligned beside the Windows notification area.
// HID protocol: VID 03F0, PID 0F98, interface MI_02; send 50 02 (64 bytes), reply 51 02, battery at byte index 2.
// Build with the existing MSVC build.bat.

#define NOMINMAX

#include <windows.h>
#include <shellapi.h>
#include <setupapi.h>
typedef LONG NTSTATUS;
#include <hidsdi.h>
#include <hidpi.h>
#include <string>
#include <vector>
#include <algorithm>
#include <cwctype>
#include <fstream> 
#define IDT_POLL       1
#define ID_REFRESH     100
#define ID_AUTOSTART   101
#define ID_EXIT        102

static void DebugLog(const std::wstring& message);
static const wchar_t* kVidPid = L"vid_03f0&pid_0f98";
static const wchar_t* kIface  = L"mi_02";
static const int      kPctOffset = 2;
static const UINT     kPollMs = 60 * 1000;
static const COLORREF kTransparentColor = RGB(255, 0, 255);

static HWND      g_mainWnd = nullptr;
static HWND      g_widget = nullptr;
static HINSTANCE g_instance = nullptr;
static UINT      g_taskbarCreated = 0;
static int       g_lastPct = -1;
static std::wstring g_status = L"starting";
static const wchar_t* kWidgetClass = L"Haste2TaskbarWidget";

// ---------------------------------------------------------------- HID layer

static std::wstring Lower(std::wstring s) {
    std::transform(s.begin(), s.end(), s.begin(), [](wchar_t c) { return (wchar_t)towlower(c); });
    return s;
}

static std::vector<std::wstring> FindDevicePaths() {
    std::vector<std::wstring> out;
    GUID hidGuid;
    HidD_GetHidGuid(&hidGuid);
    HDEVINFO set = SetupDiGetClassDevsW(&hidGuid, nullptr, nullptr, DIGCF_PRESENT | DIGCF_DEVICEINTERFACE);
    if (set == INVALID_HANDLE_VALUE) return out;

    for (DWORD i = 0;; ++i) {
        SP_DEVICE_INTERFACE_DATA did{};
        did.cbSize = sizeof(did);
        if (!SetupDiEnumDeviceInterfaces(set, nullptr, &hidGuid, i, &did)) break;

        DWORD need = 0;
        SetupDiGetDeviceInterfaceDetailW(set, &did, nullptr, 0, &need, nullptr);
        if (!need) continue;
        std::vector<BYTE> buf(need);
        auto* det = reinterpret_cast<SP_DEVICE_INTERFACE_DETAIL_DATA_W*>(buf.data());
        det->cbSize = sizeof(SP_DEVICE_INTERFACE_DETAIL_DATA_W);
        if (!SetupDiGetDeviceInterfaceDetailW(set, &did, det, need, nullptr, nullptr)) continue;

        std::wstring p = Lower(det->DevicePath);
        if (p.find(kVidPid) != std::wstring::npos && p.find(kIface) != std::wstring::npos)
            out.push_back(det->DevicePath);
    }
    SetupDiDestroyDeviceInfoList(set);
    return out;
}

static bool WaitIo(HANDLE h, OVERLAPPED& ov, DWORD ms, DWORD& got) {
    DWORD w = WaitForSingleObject(ov.hEvent, ms);
    if (w == WAIT_OBJECT_0) return GetOverlappedResult(h, &ov, &got, FALSE) != 0;
    CancelIoEx(h, &ov);
    GetOverlappedResult(h, &ov, &got, TRUE);
    return false;
}

// Returns 1..100 on success, -1 on failure.
static int QueryPath(const std::wstring& path) {
    HANDLE h = CreateFileW(path.c_str(), GENERIC_READ | GENERIC_WRITE,
                           FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING,
                           FILE_FLAG_OVERLAPPED, nullptr);
    if (h == INVALID_HANDLE_VALUE) return -1;

    int result = -1;
    PHIDP_PREPARSED_DATA pp = nullptr;
    HIDP_CAPS caps{};
    OVERLAPPED ov{};
    ov.hEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    if (!ov.hEvent) {
        CloseHandle(h);
        return -1;
    }

    if (HidD_GetPreparsedData(h, &pp)) {
        if (HidP_GetCaps(pp, &caps) == HIDP_STATUS_SUCCESS &&
            caps.OutputReportByteLength > 1 && caps.InputReportByteLength > kPctOffset) {

            std::vector<BYTE> req(caps.OutputReportByteLength, 0);
            req[0] = 0x50;
            req[1] = 0x02;
            ResetEvent(ov.hEvent);
            DWORD got = 0;
            BOOL ok = WriteFile(h, req.data(), (DWORD)req.size(), nullptr, &ov);
            bool sent = ok || (GetLastError() == ERROR_IO_PENDING && WaitIo(h, ov, 1000, got));
            if (ok) GetOverlappedResult(h, &ov, &got, TRUE);

            if (sent) {
                std::vector<BYTE> in(caps.InputReportByteLength, 0);
                ULONGLONG deadline = GetTickCount64() + 2000;
                while (GetTickCount64() < deadline) {
                    ResetEvent(ov.hEvent);
                    DWORD n = 0;
                    BOOL r = ReadFile(h, in.data(), (DWORD)in.size(), nullptr, &ov);
                    if (!r && GetLastError() != ERROR_IO_PENDING) break;
                    if (r) GetOverlappedResult(h, &ov, &n, TRUE);
                    else if (!WaitIo(h, ov, 400, n)) continue;

                    if (n > (DWORD)kPctOffset && in[0] == 0x51 && in[1] == 0x02) {
                        int v = in[kPctOffset];
                        if (v >= 1 && v <= 100) result = v;
                        break;
                    }
                }
            }
        }
        HidD_FreePreparsedData(pp);
    }
    CloseHandle(ov.hEvent);
    CloseHandle(h);
    return result;
}

static bool ReadBattery() {
    auto paths = FindDevicePaths();
    if (paths.empty()) { g_status = L"receiver not found"; return false; }
    for (auto& p : paths) {
        int v = QueryPath(p);
        if (v > 0) { g_lastPct = v; g_status = L"ok"; return true; }
    }
    g_status = L"no response (mouse asleep?)";
    return false;
}

// ---------------------------------------------------------------- Startup setting

static const wchar_t* kRunKey = L"Software\\Microsoft\\Windows\\CurrentVersion\\Run";
static const wchar_t* kRunVal = L"Haste2Tray";

static bool AutostartEnabled() {
    HKEY k;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, kRunKey, 0, KEY_READ, &k) != ERROR_SUCCESS) return false;
    bool on = RegQueryValueExW(k, kRunVal, nullptr, nullptr, nullptr, nullptr) == ERROR_SUCCESS;
    RegCloseKey(k);
    return on;
}

static void SetAutostart(bool on) {
    HKEY k;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, kRunKey, 0, KEY_WRITE, &k) != ERROR_SUCCESS) return;
    if (on) {
        wchar_t exe[MAX_PATH];
        GetModuleFileNameW(nullptr, exe, MAX_PATH);
        std::wstring v = L"\"" + std::wstring(exe) + L"\"";
        RegSetValueExW(k, kRunVal, 0, REG_SZ, (const BYTE*)v.c_str(), (DWORD)((v.size() + 1) * sizeof(wchar_t)));
    } else {
        RegDeleteValueW(k, kRunVal);
    }
    RegCloseKey(k);
}

// ---------------------------------------------------------------- Taskbar widget

static int ScaleForWindow(HWND hwnd, int value) {
    HDC dc = GetDC(hwnd);
    int dpi = dc ? GetDeviceCaps(dc, LOGPIXELSX) : 96;
    if (dc) ReleaseDC(hwnd, dc);
    if (dpi <= 0) dpi = 96;
    return MulDiv(value, dpi, 96);
}

static COLORREF TaskbarTextColor() {
    DWORD light = 1;
    HKEY key;
    if (RegOpenKeyExW(HKEY_CURRENT_USER,
        L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize",
        0, KEY_READ, &key) == ERROR_SUCCESS) {
        DWORD type = 0, size = sizeof(light);
        RegQueryValueExW(key, L"SystemUsesLightTheme", nullptr, &type, (BYTE*)&light, &size);
        RegCloseKey(key);
    }
    return light ? RGB(35, 35, 35) : RGB(245, 245, 245);
}

static void PositionTaskbarWidget() {
    if (!IsWindow(g_widget)) return;

    HWND taskbar = FindWindowW(L"Shell_TrayWnd", nullptr);
    if (!taskbar) { ShowWindow(g_widget, SW_HIDE); return; }
    HWND tray = FindWindowExW(taskbar, nullptr, L"TrayNotifyWnd", nullptr);
    if (!tray) { ShowWindow(g_widget, SW_HIDE); return; }

    RECT taskbarRect{}, trayRect{};
    if (!GetWindowRect(taskbar, &taskbarRect) || !GetWindowRect(tray, &trayRect)) {
        ShowWindow(g_widget, SW_HIDE);
        return;
    }

    int taskbarWidth = taskbarRect.right - taskbarRect.left;
    int taskbarHeight = taskbarRect.bottom - taskbarRect.top;
    if (taskbarHeight > taskbarWidth) { // vertical taskbar: this compact horizontal widget is not suitable
        ShowWindow(g_widget, SW_HIDE);
        return;
    }

    // This is a top-level popup window, so SetWindowPos uses screen coordinates.
    const int width = ScaleForWindow(taskbar, 76);
    const int scaledHeight = ScaleForWindow(taskbar, 32);
    const int height = (scaledHeight < taskbarHeight) ? scaledHeight : taskbarHeight;
    const int x = trayRect.left - width - ScaleForWindow(taskbar, 5);
    const int y = taskbarRect.top + (taskbarHeight - height) / 2;

    // Negative screen coordinates are valid on monitors arranged left of the primary display.
    SetWindowPos(g_widget, HWND_TOPMOST, x, y, width, height,
                 SWP_NOACTIVATE | SWP_SHOWWINDOW);
}

static void UpdateTaskbarWidget() {
    if (IsWindow(g_widget)) {
        PositionTaskbarWidget();
        InvalidateRect(g_widget, nullptr, TRUE);
    }
}

static void Refresh() {
    ReadBattery();
    UpdateTaskbarWidget();
}

static void ShowWidgetMenu(HWND hwnd) {
    POINT p{};
    GetCursorPos(&p);
    HMENU menu = CreatePopupMenu();
    if (!menu) return;
    AppendMenuW(menu, MF_STRING, ID_REFRESH, L"Refresh now");
    AppendMenuW(menu, MF_STRING | (AutostartEnabled() ? MF_CHECKED : 0),
                ID_AUTOSTART, L"Start with Windows");
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menu, MF_STRING, ID_EXIT, L"Exit");
    SetForegroundWindow(hwnd);
    TrackPopupMenu(menu, TPM_RIGHTBUTTON, p.x, p.y, 0, hwnd, nullptr);
    PostMessageW(hwnd, WM_NULL, 0, 0);
    DestroyMenu(menu);
}

static LRESULT CALLBACK TaskbarWidgetProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
    case WM_NCCREATE:
    DebugLog(L"WM_NCCREATE received");
    return DefWindowProcW(hwnd, msg, wParam, lParam);

    case WM_CREATE:
    DebugLog(L"WM_CREATE received");
    return 0;
    
    case WM_ERASEBKGND: {
        RECT rc{};
        GetClientRect(hwnd, &rc);
        HBRUSH brush = CreateSolidBrush(kTransparentColor);
        FillRect((HDC)wParam, &rc, brush);
        DeleteObject(brush);
        return 1;
    }
    case WM_PAINT: {
        PAINTSTRUCT ps{};
        HDC dc = BeginPaint(hwnd, &ps);
        RECT rc{};
        GetClientRect(hwnd, &rc);
        HBRUSH bg = CreateSolidBrush(kTransparentColor);
        FillRect(dc, &rc, bg);
        DeleteObject(bg);

        const int dpi = GetDeviceCaps(dc, LOGPIXELSX);
        const int s = dpi > 0 ? dpi : 96;
        auto px = [s](int v) { return MulDiv(v, s, 96); };
        int h = rc.bottom - rc.top;
        int iconX = px(7);
        int iconW = px(17);
        int iconH = px(10);
        int iconY = (h - iconH) / 2;

        COLORREF color = (g_lastPct >= 0 && g_lastPct <= 15)
            ? RGB(255, 255, 255) : TaskbarTextColor();
        HPEN pen = CreatePen(PS_SOLID, (px(1) > 1 ? px(1) : 1), color);
        HGDIOBJ oldPen = SelectObject(dc, pen);
        HGDIOBJ oldBrush = SelectObject(dc, GetStockObject(NULL_BRUSH));
        Rectangle(dc, iconX, iconY, iconX + iconW, iconY + iconH);
        Rectangle(dc, iconX + iconW, iconY + px(3), iconX + iconW + px(2), iconY + iconH - px(3));
        SelectObject(dc, oldBrush);
        SelectObject(dc, oldPen);
        DeleteObject(pen);

        if (g_lastPct > 0) {
            int innerW = (iconW - px(4)) * g_lastPct / 100;
            if (innerW < 1) innerW = 1;
            RECT fill{ iconX + px(2), iconY + px(2), iconX + px(2) + innerW, iconY + iconH - px(2) };
            HBRUSH level = CreateSolidBrush(color);
            FillRect(dc, &fill, level);
            DeleteObject(level);
        }

        std::wstring text = g_lastPct >= 0 ? std::to_wstring(g_lastPct) + L"%" : L"--";
        HFONT font = CreateFontW(px(15), 0, 0, 0, FW_SEMIBOLD, FALSE, FALSE, FALSE,
                                 DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                                 ANTIALIASED_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
        HGDIOBJ oldFont = SelectObject(dc, font);
        SetBkMode(dc, TRANSPARENT);
        SetTextColor(dc, color);
        RECT textRc{ iconX + iconW + px(7), 0, rc.right - px(3), h };
        DrawTextW(dc, text.c_str(), -1, &textRc, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
        SelectObject(dc, oldFont);
        DeleteObject(font);
        EndPaint(hwnd, &ps);
        return 0;
    }
    case WM_LBUTTONUP:
        Refresh();
        return 0;
    case WM_RBUTTONUP:
        ShowWidgetMenu(hwnd);
        return 0;
    case WM_COMMAND:
        switch (LOWORD(wParam)) {
        case ID_REFRESH:
            Refresh();
            return 0;
        case ID_AUTOSTART:
            SetAutostart(!AutostartEnabled());
            return 0;
        case ID_EXIT:
            if (IsWindow(g_mainWnd)) DestroyWindow(g_mainWnd);
            return 0;
        }
        break;
    case WM_NCDESTROY:
        if (g_widget == hwnd) g_widget = nullptr;
        break;
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}



static bool CreateTaskbarWidget() {
    DebugLog(L"CreateTaskbarWidget called");

    HWND taskbar = FindWindowW(L"Shell_TrayWnd", nullptr);

    if (!taskbar) {
        DebugLog(L"ERROR: Shell_TrayWnd not found");
        return false;
    }

    DebugLog(L"Shell_TrayWnd found");

    if (!g_widget || !IsWindow(g_widget)) {
        SetLastError(ERROR_SUCCESS);
        g_widget = CreateWindowExW(
            WS_EX_LAYERED | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE,
            kWidgetClass, L"Haste 2 Battery",
            WS_POPUP,
            0, 0, 1, 1,
            nullptr, nullptr, g_instance, nullptr
        );

        if (!g_widget) {
            const DWORD error = GetLastError();
            DebugLog(L"CreateWindowExW failed; GetLastError=" + std::to_wstring(error));
            return false;
        }

        DebugLog(L"CreateWindowExW succeeded");

        if (!SetLayeredWindowAttributes(
            g_widget, kTransparentColor, 0, LWA_COLORKEY)) {
            DebugLog(L"ERROR: SetLayeredWindowAttributes failed");
        }
    }

    HWND tray = FindWindowExW(
        taskbar, nullptr, L"TrayNotifyWnd", nullptr
    );

    if (!tray) {
        DebugLog(L"ERROR: TrayNotifyWnd not found");
    } else {
        DebugLog(L"TrayNotifyWnd found");
    }

    PositionTaskbarWidget();

    DebugLog(L"PositionTaskbarWidget completed");

    return true;
}

// ---------------------------------------------------------------- Main hidden window

static void DebugLog(const std::wstring& message) {
    //std::wofstream log(
     //   L"C:\\haste2-tray\\haste2-widget-debug.txt",
     //  std::ios::app
    //);

    //log << message << L"\n";
}

static LRESULT CALLBACK MainWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    if (msg == g_taskbarCreated) { // Explorer restarted; reposition the independent popup.
        if (!IsWindow(g_widget)) CreateTaskbarWidget();
        Refresh();
        return 0;
    }
    switch (msg) {
    case WM_TIMER:
        if (wParam == IDT_POLL) Refresh();
        return 0;
    case WM_SETTINGCHANGE:
    case WM_DISPLAYCHANGE:
        PositionTaskbarWidget();
        return 0;
    case WM_DESTROY:
        KillTimer(hwnd, IDT_POLL);
        if (IsWindow(g_widget)) DestroyWindow(g_widget);
        g_widget = nullptr;
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

int WINAPI wWinMain(HINSTANCE hi, HINSTANCE, PWSTR, int) {
    HANDLE mutex = CreateMutexW(nullptr, TRUE, L"Haste2TrayBatterySingleInstance");
    if (GetLastError() == ERROR_ALREADY_EXISTS) {
        if (mutex) CloseHandle(mutex);
        return 0;
    }

    g_instance = hi;
    g_taskbarCreated = RegisterWindowMessageW(L"TaskbarCreated");

    WNDCLASSW widgetClass{};
    widgetClass.lpfnWndProc = TaskbarWidgetProc;
    widgetClass.hInstance = hi;
    widgetClass.lpszClassName = kWidgetClass;
    widgetClass.hCursor = LoadCursorW(nullptr, IDC_HAND);
    if (!RegisterClassW(&widgetClass)) {
    DebugLog(L"Widget class registration failed: " +
             std::to_wstring(GetLastError()));
    return 1;
}

DebugLog(L"Widget class registered successfully");

    WNDCLASSW mainClass{};
    mainClass.lpfnWndProc = MainWndProc;
    mainClass.hInstance = hi;
    mainClass.lpszClassName = L"Haste2TaskbarMainWnd";
    RegisterClassW(&mainClass);

    HWND hw = CreateWindowW(mainClass.lpszClassName, L"", 0,
                            0, 0, 0, 0, nullptr, nullptr, hi, nullptr);
    if (!hw) {
        if (mutex) CloseHandle(mutex);
        return 1;
    }
    g_mainWnd = hw;

    Refresh();
    CreateTaskbarWidget();
    SetTimer(hw, IDT_POLL, kPollMs, nullptr);

    MSG msg{};
    while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    if (mutex) CloseHandle(mutex);
    return 0;
}

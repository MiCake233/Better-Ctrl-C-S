#include "app.h"

#include "audio.h"
#include "config.h"
#include "notify.h"
#include "resource.h"
#include "settings.h"
#include "util.h"

#include <commctrl.h>
#include <shellapi.h>

Config    g_config;
HINSTANCE g_hinst;
HWND      g_hwndMain;
UINT      g_dpi = 96;

static HHOOK g_hook;
static HICON g_icon;
static BOOL  g_trayAdded;
static BOOL  g_quitting;
static UINT  g_taskbarCreated;
static BOOL  g_keyDown[256];
static DWORD g_keyTick[256];

static void TrayAdd(void);
static void TrayRemove(void);

static void ShowHelp(void)
{
    MessageBoxW(NULL,
                L"Better Ctrl+C/S " APP_VERSION L"\n\n"
                L"A lightweight tray utility that acknowledges Ctrl+C and "
                L"Ctrl+S.\n\n"
                L"Usage:\n"
                L"  BetterCtrlCS.exe                 Start and open settings\n"
                L"  BetterCtrlCS.exe --background    Start without opening "
                L"settings\n"
                L"  BetterCtrlCS.exe --settings      Open settings\n"
                L"  BetterCtrlCS.exe --version       Show the version\n"
                L"  BetterCtrlCS.exe --help          Show this help\n\n"
                L"Feedback only. This program does not verify that a copy or "
                L"save succeeded.",
                APP_TITLE, MB_ICONINFORMATION | MB_SETFOREGROUND);
}

static BOOL HasArg(int argc, wchar_t** argv, const wchar_t* name)
{
    int i;
    for (i = 1; i < argc; i++) {
        if (_wcsicmp(argv[i], name) == 0)
            return TRUE;
    }
    return FALSE;
}

static void TrayTip(wchar_t* tip, int cch)
{
    wsprintfW(tip, L"Better Ctrl+C/S - %s",
              g_config.enabled ? L"enabled" : L"disabled");
    tip[cch - 1] = 0;
}

static void TrayAdd(void)
{
    NOTIFYICONDATAW nid;

    ZeroMemory(&nid, sizeof(nid));
    nid.cbSize = sizeof(nid);
    nid.hWnd = g_hwndMain;
    nid.uID = 1;
    nid.uFlags = NIF_MESSAGE | NIF_ICON | NIF_TIP;
    nid.uCallbackMessage = WM_APP_TRAY;
    nid.hIcon = g_icon;
    TrayTip(nid.szTip, (int)(sizeof(nid.szTip) / sizeof(nid.szTip[0])));

    g_trayAdded = Shell_NotifyIconW(NIM_ADD, &nid) != FALSE;
    if (!g_trayAdded)
        UtilLog(L"Tray icon could not be created");
}

static void TrayRemove(void)
{
    NOTIFYICONDATAW nid;

    if (!g_trayAdded)
        return;
    ZeroMemory(&nid, sizeof(nid));
    nid.cbSize = sizeof(nid);
    nid.hWnd = g_hwndMain;
    nid.uID = 1;
    Shell_NotifyIconW(NIM_DELETE, &nid);
    g_trayAdded = FALSE;
}

void AppUpdateTray(void)
{
    NOTIFYICONDATAW nid;

    if (!g_trayAdded) {
        TrayAdd();
        return;
    }

    ZeroMemory(&nid, sizeof(nid));
    nid.cbSize = sizeof(nid);
    nid.hWnd = g_hwndMain;
    nid.uID = 1;
    nid.uFlags = NIF_TIP;
    TrayTip(nid.szTip, (int)(sizeof(nid.szTip) / sizeof(nid.szTip[0])));
    Shell_NotifyIconW(NIM_MODIFY, &nid);
}

static void ShowTrayMenu(void)
{
    HMENU menu;
    POINT pt;
    int cmd;

    menu = CreatePopupMenu();
    if (!menu)
        return;

    AppendMenuW(menu, MF_STRING, IDM_SETTINGS, L"Settings...");
    AppendMenuW(menu, MF_STRING | (g_config.enabled ? MF_CHECKED : 0),
                IDM_ENABLE, L"Enabled");
    AppendMenuW(menu, MF_SEPARATOR, 0, NULL);
    AppendMenuW(menu, MF_STRING, IDM_EXIT, L"Exit");

    GetCursorPos(&pt);
    SetForegroundWindow(g_hwndMain);
    cmd = (int)TrackPopupMenu(menu,
                              TPM_RETURNCMD | TPM_RIGHTBUTTON | TPM_NONOTIFY,
                              pt.x, pt.y, 0, g_hwndMain, NULL);
    DestroyMenu(menu);
    PostMessageW(g_hwndMain, WM_NULL, 0, 0);

    if (cmd)
        SendMessageW(g_hwndMain, WM_COMMAND, (WPARAM)cmd, 0);
}

static void ToggleEnabled(void)
{
    BOOL previous = g_config.enabled;

    g_config.enabled = !g_config.enabled;
    if (!ConfigSave(&g_config, g_iniPath)) {
        g_config.enabled = previous;
        MessageBoxW(NULL, L"Could not write the configuration file.",
                    APP_TITLE, MB_ICONERROR);
        return;
    }
    AppUpdateTray();
}

static void OnTrigger(UINT vk)
{
    BOOL isCopy = (vk == 'C');
    int feedback;
    const AudioClip* clip;

    if (!g_config.enabled)
        return;
    if (isCopy ? !g_config.c_enabled : !g_config.s_enabled)
        return;

    feedback = isCopy ? g_config.c_feedback : g_config.s_feedback;

    if (feedback & FEEDBACK_SOUND) {
        clip = g_config.custom_sound ? AudioCustom() : NULL;
        if (!clip)
            clip = AudioBeep();
        if (clip)
            AudioPlay(clip, g_config.volume);
    }

    if (feedback & FEEDBACK_NOTIFY) {
        NotifyShow(isCopy ? g_config.text_copy : g_config.text_save);
    }
}

static LRESULT CALLBACK LowLevelKeyboardProc(int nCode, WPARAM wParam,
                                             LPARAM lParam)
{
    if (nCode == HC_ACTION) {
        const KBDLLHOOKSTRUCT* k = (const KBDLLHOOKSTRUCT*)lParam;
        UINT vk = k->vkCode;

        if (wParam == WM_KEYDOWN || wParam == WM_SYSKEYDOWN) {
            DWORD now = GetTickCount();
            if (vk < 256 && (!g_keyDown[vk] || now - g_keyTick[vk] > 5000)) {
                g_keyDown[vk] = TRUE;
                g_keyTick[vk] = now;

                if (vk == 'C' || vk == 'S') {
                    BOOL ctrl =
                        g_keyDown[VK_CONTROL] || g_keyDown[VK_LCONTROL] ||
                        g_keyDown[VK_RCONTROL] ||
                        (GetAsyncKeyState(VK_CONTROL) & 0x8000) != 0;
                    BOOL alt = g_keyDown[VK_MENU] || g_keyDown[VK_LMENU] ||
                               g_keyDown[VK_RMENU] ||
                               (GetAsyncKeyState(VK_MENU) & 0x8000) != 0;

                    if (ctrl && !alt && g_config.enabled) {
                        BOOL enabled = (vk == 'C') ? g_config.c_enabled
                                                   : g_config.s_enabled;
                        if (enabled) {
                            PostMessageW(g_hwndMain, WM_APP_TRIGGER, vk, 0);
                        }
                    }
                }
            }
        } else if (wParam == WM_KEYUP || wParam == WM_SYSKEYUP) {
            if (vk < 256)
                g_keyDown[vk] = FALSE;
        }
    }

    return CallNextHookEx(g_hook, nCode, wParam, lParam);
}

void AppRequestExit(void)
{
    if (g_quitting)
        return;
    g_quitting = TRUE;
    PostQuitMessage(0);
}

static LRESULT CALLBACK MainProc(HWND hwnd, UINT msg, WPARAM wParam,
                                 LPARAM lParam)
{
    if (g_taskbarCreated && msg == g_taskbarCreated) {
        g_trayAdded = FALSE;
        TrayAdd();
        return 0;
    }

    switch (msg) {
    case WM_APP_TRIGGER:
        OnTrigger((UINT)wParam);
        return 0;

    case WM_APP_SHOWSETTINGS:
        SettingsShow();
        return 0;

    case WM_APP_TRAY:
        switch ((UINT)lParam) {
        case WM_LBUTTONDBLCLK:
            SettingsShow();
            break;
        case WM_RBUTTONUP:
        case WM_CONTEXTMENU:
            ShowTrayMenu();
            break;
        default:
            break;
        }
        return 0;

    case WM_COMMAND:
        switch (LOWORD(wParam)) {
        case IDM_SETTINGS:
            SettingsShow();
            break;
        case IDM_ENABLE:
            ToggleEnabled();
            break;
        case IDM_EXIT:
            AppRequestExit();
            break;
        default:
            break;
        }
        return 0;

    case WM_QUERYENDSESSION:
        return TRUE;

    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;

    default:
        break;
    }

    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

static BOOL RegisterMainClass(void)
{
    WNDCLASSEXW wc;

    ZeroMemory(&wc, sizeof(wc));
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = MainProc;
    wc.hInstance = g_hinst;
    wc.hCursor = LoadCursorW(NULL, (LPCWSTR)IDC_ARROW);
    wc.hIcon = g_icon;
    wc.lpszClassName = MAIN_CLASS;
    return RegisterClassExW(&wc) != 0;
}

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance,
                   LPSTR lpCmdLine, int nCmdShow)
{
    static const wchar_t mutexName[] = L"Local\\BetterCtrlCS.Instance";
    HANDLE mutex;
    int argc = 0;
    wchar_t** argv;
    BOOL background = FALSE;
    BOOL firstRun;
    MSG msg;
    HDC hdc;
    INITCOMMONCONTROLSEX icc;
    int exitCode = 0;

    (void)hPrevInstance;
    (void)lpCmdLine;
    (void)nCmdShow;

    g_hinst = hInstance;

    argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    if (argv) {
        if (HasArg(argc, argv, L"--help")) {
            ShowHelp();
            LocalFree(argv);
            return 0;
        }
        if (HasArg(argc, argv, L"--version")) {
            MessageBoxW(NULL, L"Better Ctrl+C/S " APP_VERSION, APP_TITLE,
                        MB_ICONINFORMATION | MB_SETFOREGROUND);
            LocalFree(argv);
            return 0;
        }
        background = HasArg(argc, argv, L"--background");
        if (HasArg(argc, argv, L"--settings"))
            background = FALSE;
    }

    hdc = GetDC(NULL);
    if (hdc) {
        g_dpi = (UINT)GetDeviceCaps(hdc, LOGPIXELSX);
        ReleaseDC(NULL, hdc);
    }
    if (g_dpi == 0)
        g_dpi = 96;

    mutex = CreateMutexW(NULL, FALSE, mutexName);
    if (mutex && GetLastError() == ERROR_ALREADY_EXISTS) {
        int i;
        for (i = 0; i < 40; i++) {
            HWND existing = FindWindowW(MAIN_CLASS, NULL);
            if (existing) {
                PostMessageW(existing, WM_APP_SHOWSETTINGS, 0, 0);
                break;
            }
            Sleep(50);
        }
        if (argv)
            LocalFree(argv);
        CloseHandle(mutex);
        return 0;
    }

    if (!UtilInitPaths()) {
        MessageBoxW(NULL,
                    L"Could not access the configuration directory. "
                    L"Check your permissions and try again.",
                    APP_TITLE, MB_ICONERROR | MB_SETFOREGROUND);
        if (argv)
            LocalFree(argv);
        if (mutex)
            CloseHandle(mutex);
        return 1;
    }

    firstRun = !UtilFileExists(g_iniPath);
    ConfigSetDefaults(&g_config);
    if (!firstRun) {
        ConfigLoad(&g_config, g_iniPath);
    } else {
        ConfigSave(&g_config, g_iniPath);
    }

    UtilLog(L"Started " APP_VERSION);

    icc.dwSize = sizeof(icc);
    icc.dwICC = ICC_BAR_CLASSES | ICC_STANDARD_CLASSES;
    InitCommonControlsEx(&icc);

    g_icon = LoadIconW(g_hinst, MAKEINTRESOURCEW(IDI_APP));
    if (!RegisterMainClass()) {
        UtilLog(L"RegisterMainClass failed (%lu)", GetLastError());
        if (argv)
            LocalFree(argv);
        if (mutex)
            CloseHandle(mutex);
        return 1;
    }

    g_taskbarCreated = RegisterWindowMessageW(L"TaskbarCreated");

    g_hwndMain = CreateWindowExW(0, MAIN_CLASS, APP_TITLE, WS_POPUP, 0, 0, 0,
                                 0, NULL, NULL, g_hinst, NULL);
    if (!g_hwndMain) {
        UtilLog(L"CreateWindowEx main failed (%lu)", GetLastError());
        if (argv)
            LocalFree(argv);
        if (mutex)
            CloseHandle(mutex);
        return 1;
    }

    if (!SettingsInit(g_hinst) || !NotifyInit(g_hinst)) {
        UtilLog(L"UI initialization failed");
        MessageBoxW(NULL, L"Could not initialize the user interface.",
                    APP_TITLE, MB_ICONERROR);
        DestroyWindow(g_hwndMain);
        if (argv)
            LocalFree(argv);
        if (mutex)
            CloseHandle(mutex);
        return 1;
    }

    AudioInit();

    g_hook = SetWindowsHookExW(WH_KEYBOARD_LL, LowLevelKeyboardProc,
                               GetModuleHandleW(NULL), 0);
    if (!g_hook) {
        DWORD err = GetLastError();
        wchar_t text[256];
        UtilLog(L"SetWindowsHookEx failed (%lu)", err);
        wsprintfW(text,
                  L"Could not install the keyboard hook (error %lu).\n\n"
                  L"The program cannot run without it.",
                  err);
        MessageBoxW(NULL, text, APP_TITLE, MB_ICONERROR);
        AudioShutdown();
        NotifyShutdown();
        SettingsShutdown();
        DestroyWindow(g_hwndMain);
        if (argv)
            LocalFree(argv);
        if (mutex)
            CloseHandle(mutex);
        return 1;
    }

    TrayAdd();

    if (firstRun) {
        if (!UtilAutostartSet(TRUE))
            UtilLog(L"Could not enable autostart");
    } else if (UtilAutostartEnabled() && !UtilAutostartIsCurrent()) {
        if (!UtilAutostartSet(TRUE))
            UtilLog(L"Could not refresh the autostart path");
    }

    if (!background)
        SettingsShow();

    while (GetMessageW(&msg, NULL, 0, 0) > 0) {
        HWND settings = SettingsWindow();
        if (settings && IsWindowVisible(settings) &&
            IsDialogMessageW(settings, &msg)) {
            continue;
        }
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    exitCode = (int)msg.wParam;

    if (g_hook) {
        UnhookWindowsHookEx(g_hook);
        g_hook = NULL;
    }
    TrayRemove();
    SettingsShutdown();
    NotifyShutdown();
    AudioShutdown();
    DestroyWindow(g_hwndMain);
    g_hwndMain = NULL;

    if (argv)
        LocalFree(argv);
    if (mutex)
        CloseHandle(mutex);
    return exitCode;
}

#include "settings.h"

#include "audio.h"
#include "config.h"
#include "resource.h"
#include "util.h"

#include <commctrl.h>
#include <commdlg.h>

#define CLIENT_W 440
#define CLIENT_H 488

static HWND g_win;
static Config g_draft;
static AudioClip g_pick;
static BOOL g_pickValid;
static BOOL g_pickDirty;
static wchar_t g_pickName[80];
static BOOL g_startup;
static HFONT g_fontUI;
static HFONT g_fontSmall;
static HFONT g_fontTitle;
static HFONT g_fontGroup;

#define S(v) UtilScale(v)

static HFONT MakeFont(int pt, int weight)
{
    return CreateFontW(-MulDiv(pt, (int)g_dpi, 72), 0, 0, 0, weight, FALSE,
                       FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
                       CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                       DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
}

static HWND AddControl(HWND parent, const wchar_t* cls, const wchar_t* text,
                       DWORD style, int x, int y, int w, int h, int id,
                       HFONT font)
{
    HWND ctl = CreateWindowExW(0, cls, text, WS_CHILD | WS_VISIBLE | style,
                               S(x), S(y), S(w), S(h), parent,
                               (HMENU)(INT_PTR)id, g_hinst, NULL);
    if (ctl && font)
        SendMessageW(ctl, WM_SETFONT, (WPARAM)font, TRUE);
    return ctl;
}

static int FeedbackToIndex(int f)
{
    if ((f & (FEEDBACK_SOUND | FEEDBACK_NOTIFY)) ==
        (FEEDBACK_SOUND | FEEDBACK_NOTIFY))
        return 2;
    if (f & FEEDBACK_NOTIFY)
        return 1;
    return 0;
}

static int IndexToFeedback(int index)
{
    if (index == 2)
        return FEEDBACK_SOUND | FEEDBACK_NOTIFY;
    if (index == 1)
        return FEEDBACK_NOTIFY;
    return FEEDBACK_SOUND;
}

static void UpdateVolumeLabel(void)
{
    wchar_t text[16];
    wsprintfW(text, L"%d%%", g_draft.volume);
    SetWindowTextW(GetDlgItem(g_win, IDC_VOL_VALUE), text);
}

static void UpdateSoundLabel(void)
{
    wchar_t text[256];

    if (!g_draft.custom_sound) {
        wcscpy(text, L"Built-in beep");
    } else if (g_pickValid && g_pickName[0]) {
        wsprintfW(text, L"Custom WAV: %s", g_pickName);
    } else {
        wcscpy(text, L"Custom WAV: custom.wav");
    }
    text[255] = 0;
    SetWindowTextW(GetDlgItem(g_win, IDC_SOUND_VALUE), text);
}

static void UpdateEnabledStates(void)
{
    BOOL master = g_draft.enabled;

    EnableWindow(GetDlgItem(g_win, IDC_C_ENABLE), master);
    EnableWindow(GetDlgItem(g_win, IDC_S_ENABLE), master);
    EnableWindow(GetDlgItem(g_win, IDC_C_EFFECT), master && g_draft.c_enabled);
    EnableWindow(GetDlgItem(g_win, IDC_S_EFFECT), master && g_draft.s_enabled);
}

static void UpdateControls(void)
{
    CheckDlgButton(g_win, IDC_ENABLE,
                   g_draft.enabled ? BST_CHECKED : BST_UNCHECKED);
    CheckDlgButton(g_win, IDC_C_ENABLE,
                   g_draft.c_enabled ? BST_CHECKED : BST_UNCHECKED);
    CheckDlgButton(g_win, IDC_S_ENABLE,
                   g_draft.s_enabled ? BST_CHECKED : BST_UNCHECKED);

    SendMessageW(GetDlgItem(g_win, IDC_C_EFFECT), CB_SETCURSEL,
                 (WPARAM)FeedbackToIndex(g_draft.c_feedback), 0);
    SendMessageW(GetDlgItem(g_win, IDC_S_EFFECT), CB_SETCURSEL,
                 (WPARAM)FeedbackToIndex(g_draft.s_feedback), 0);
    SendMessageW(GetDlgItem(g_win, IDC_VOLUME), TBM_SETPOS, TRUE,
                 (LPARAM)g_draft.volume);

    UpdateVolumeLabel();
    UpdateSoundLabel();
    CheckDlgButton(g_win, IDC_STARTUP,
                   g_startup ? BST_CHECKED : BST_UNCHECKED);
    UpdateEnabledStates();
}

static void FreePick(void)
{
    AudioFree(&g_pick);
    g_pickValid = FALSE;
    g_pickDirty = FALSE;
    g_pickName[0] = 0;
}

static void ResetDraft(void)
{
    g_draft = g_config;
    g_startup = UtilAutostartEnabled();
    FreePick();

    /* A stale "CustomSound=1" with no readable file falls back to the tone. */
    if (g_draft.custom_sound && !AudioCustom())
        g_draft.custom_sound = FALSE;

    UpdateControls();
}

static void PlayDraft(void)
{
    const AudioClip* clip = NULL;

    if (g_pickValid) {
        clip = &g_pick;
    } else if (g_draft.custom_sound) {
        clip = AudioCustom();
    }
    if (!clip)
        clip = AudioBeep();
    if (clip)
        AudioPlay(clip, g_draft.volume);
}

static void ChooseSound(void)
{
    wchar_t path[MAX_PATH] = L"";
    OPENFILENAMEW ofn;
    AudioClip clip;
    wchar_t err[256];
    const wchar_t* base;

    ZeroMemory(&ofn, sizeof(ofn));
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = g_win;
    ofn.hInstance = g_hinst;
    ofn.lpstrFilter = L"Wave audio (*.wav)\0*.wav\0"
                      L"All files (*.*)\0*.*\0";
    ofn.lpstrFile = path;
    ofn.nMaxFile = MAX_PATH;
    ofn.lpstrTitle = L"Choose a WAV file";
    ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR |
                OFN_HIDEREADONLY;

    if (!GetOpenFileNameW(&ofn))
        return;

    ZeroMemory(&clip, sizeof(clip));
    if (!AudioLoadWav(path, &clip, err, 256)) {
        MessageBoxW(g_win, err, L"Unsupported audio", MB_ICONWARNING);
        return;
    }

    AudioFree(&g_pick);
    g_pick = clip;
    g_pickValid = TRUE;
    g_pickDirty = TRUE;

    base = wcsrchr(path, L'\\');
    base = base ? base + 1 : path;
    wcsncpy(g_pickName, base, 79);
    g_pickName[79] = 0;

    g_draft.custom_sound = TRUE;
    UpdateSoundLabel();
    AudioPlay(&g_pick, g_draft.volume);
}

static void ResetSound(void)
{
    g_draft.custom_sound = FALSE;
    FreePick();
    UpdateSoundLabel();
}

static void HideDiscard(void)
{
    FreePick();
    if (g_win)
        ShowWindow(g_win, SW_HIDE);
}

static void ReadControls(void)
{
    int cIndex = (int)SendMessageW(GetDlgItem(g_win, IDC_C_EFFECT),
                                   CB_GETCURSEL, 0, 0);
    int sIndex = (int)SendMessageW(GetDlgItem(g_win, IDC_S_EFFECT),
                                   CB_GETCURSEL, 0, 0);

    if (cIndex < 0)
        cIndex = 0;
    if (sIndex < 0)
        sIndex = 0;

    g_draft.enabled = IsDlgButtonChecked(g_win, IDC_ENABLE) == BST_CHECKED;
    g_draft.c_enabled = IsDlgButtonChecked(g_win, IDC_C_ENABLE) == BST_CHECKED;
    g_draft.s_enabled = IsDlgButtonChecked(g_win, IDC_S_ENABLE) == BST_CHECKED;
    g_draft.c_feedback = IndexToFeedback(cIndex);
    g_draft.s_feedback = IndexToFeedback(sIndex);
    g_draft.volume = (int)SendMessageW(GetDlgItem(g_win, IDC_VOLUME),
                                       TBM_GETPOS, 0, 0);
    g_startup = IsDlgButtonChecked(g_win, IDC_STARTUP) == BST_CHECKED;
}

static void Commit(void)
{
    ReadControls();

    if (g_draft.custom_sound && g_pickDirty) {
        if (!g_pickValid || !AudioSaveWav(&g_pick, g_customWavPath)) {
            MessageBoxW(g_win,
                        L"Could not write the custom sound file.",
                        L"Better Ctrl+C/S", MB_ICONERROR);
            return;
        }
    }

    if (!UtilAutostartSet(g_startup)) {
        MessageBoxW(g_win,
                    L"Could not update the Windows startup entry. "
                    L"Check your permissions and try again.",
                    L"Better Ctrl+C/S", MB_ICONERROR);
        return;
    }

    if (!ConfigSave(&g_draft, g_iniPath)) {
        MessageBoxW(g_win, L"Could not write the configuration file.",
                    L"Better Ctrl+C/S", MB_ICONERROR);
        return;
    }

    g_config = g_draft;
    AudioReloadCustom();
    AppUpdateTray();
    HideDiscard();
}

static void OnCommand(int id)
{
    switch (id) {
    case IDC_ENABLE:
        g_draft.enabled =
            IsDlgButtonChecked(g_win, IDC_ENABLE) == BST_CHECKED;
        UpdateEnabledStates();
        break;

    case IDC_C_ENABLE:
        g_draft.c_enabled =
            IsDlgButtonChecked(g_win, IDC_C_ENABLE) == BST_CHECKED;
        UpdateEnabledStates();
        break;

    case IDC_S_ENABLE:
        g_draft.s_enabled =
            IsDlgButtonChecked(g_win, IDC_S_ENABLE) == BST_CHECKED;
        UpdateEnabledStates();
        break;

    case IDC_TEST:
        PlayDraft();
        break;

    case IDC_CHOOSE:
        ChooseSound();
        break;

    case IDC_RESET:
        ResetSound();
        break;

    case IDOK:
        Commit();
        break;

    case IDCANCEL:
        HideDiscard();
        break;

    default:
        break;
    }
}

static LRESULT CALLBACK SettingsProc(HWND hwnd, UINT msg, WPARAM wParam,
                                     LPARAM lParam)
{
    switch (msg) {
    case WM_CREATE:
        AddControl(hwnd, L"STATIC", L"Better Ctrl+C/S", SS_LEFT, 16, 14, 408,
                   28, IDC_TITLE, g_fontTitle);
        AddControl(hwnd, L"STATIC",
                   L"Advisory feedback only.\n"
                   L"It does not confirm that the copy or save succeeded.",
                   SS_LEFT, 16, 46, 408, 32, IDC_HINT, g_fontSmall);

        AddControl(hwnd, L"BUTTON", L"Indicators", BS_GROUPBOX, 16, 86, 408,
                   144, 0, g_fontGroup);
        AddControl(hwnd, L"BUTTON", L"Enable indicators",
                   BS_AUTOCHECKBOX | WS_TABSTOP, 30, 104, 200, 22, IDC_ENABLE,
                   g_fontUI);
        AddControl(hwnd, L"BUTTON", L"Ctrl+C",
                   BS_AUTOCHECKBOX | WS_TABSTOP, 30, 132, 100, 22,
                   IDC_C_ENABLE, g_fontUI);
        AddControl(hwnd, L"BUTTON", L"Ctrl+S",
                   BS_AUTOCHECKBOX | WS_TABSTOP, 30, 164, 100, 22,
                   IDC_S_ENABLE, g_fontUI);

        {
            HWND c = AddControl(hwnd, L"COMBOBOX", L"",
                                CBS_DROPDOWNLIST | WS_VSCROLL | WS_TABSTOP,
                                250, 128, 160, 200, IDC_C_EFFECT, g_fontUI);
            SendMessageW(c, CB_ADDSTRING, 0, (LPARAM)L"Sound");
            SendMessageW(c, CB_ADDSTRING, 0, (LPARAM)L"Notification");
            SendMessageW(c, CB_ADDSTRING, 0, (LPARAM)L"Sound + notification");
            SendMessageW(c, CB_SETCURSEL, 0, 0);
        }
        {
            HWND c = AddControl(hwnd, L"COMBOBOX", L"",
                                CBS_DROPDOWNLIST | WS_VSCROLL | WS_TABSTOP,
                                250, 160, 160, 200, IDC_S_EFFECT, g_fontUI);
            SendMessageW(c, CB_ADDSTRING, 0, (LPARAM)L"Sound");
            SendMessageW(c, CB_ADDSTRING, 0, (LPARAM)L"Notification");
            SendMessageW(c, CB_ADDSTRING, 0, (LPARAM)L"Sound + notification");
            SendMessageW(c, CB_SETCURSEL, 0, 0);
        }

        AddControl(hwnd, L"STATIC",
                   L"Each shortcut can use sound, notification, or both.",
                   SS_LEFT, 30, 198, 360, 18, IDC_EFFECT_NOTE, g_fontSmall);

        AddControl(hwnd, L"BUTTON", L"Sound", BS_GROUPBOX, 16, 242, 408, 120,
                   0, g_fontGroup);
        AddControl(hwnd, L"STATIC", L"Volume", SS_LEFT, 30, 266, 55, 20,
                   IDC_VOL_LABEL, g_fontUI);
        {
            HWND tb = AddControl(hwnd, L"msctls_trackbar32", L"",
                                 TBS_HORZ | TBS_NOTICKS | WS_TABSTOP, 88,
                                 260, 210, 30, IDC_VOLUME, g_fontUI);
            SendMessageW(tb, TBM_SETRANGE, TRUE, MAKELPARAM(0, 100));
            SendMessageW(tb, TBM_SETPAGESIZE, 0, 5);
        }
        AddControl(hwnd, L"STATIC", L"60%", SS_LEFT, 306, 266, 70, 20,
                   IDC_VOL_VALUE, g_fontUI);
        AddControl(hwnd, L"STATIC", L"Sound", SS_LEFT, 30, 298, 55, 20,
                   IDC_SOUND_LABEL, g_fontUI);
        AddControl(hwnd, L"STATIC", L"Built-in beep",
                   SS_LEFT | SS_ENDELLIPSIS, 88, 298, 300, 20, IDC_SOUND_VALUE,
                   g_fontUI);
        AddControl(hwnd, L"BUTTON", L"Choose WAV...",
                   BS_PUSHBUTTON | WS_TABSTOP, 88, 324, 104, 26, IDC_CHOOSE,
                   g_fontUI);
        AddControl(hwnd, L"BUTTON", L"Reset", BS_PUSHBUTTON | WS_TABSTOP, 200,
                   324, 72, 26, IDC_RESET, g_fontUI);
        AddControl(hwnd, L"BUTTON", L"Test", BS_PUSHBUTTON | WS_TABSTOP, 280,
                   324, 72, 26, IDC_TEST, g_fontUI);

        AddControl(hwnd, L"BUTTON", L"Startup", BS_GROUPBOX, 16, 372, 408, 64,
                   0, g_fontGroup);
        AddControl(hwnd, L"BUTTON", L"Start with Windows",
                   BS_AUTOCHECKBOX | WS_TABSTOP, 30, 392, 200, 22, IDC_STARTUP,
                   g_fontUI);
        AddControl(hwnd, L"STATIC",
                   L"Starts hidden in the system tray at sign-in.",
                   SS_LEFT, 30, 414, 360, 18, IDC_STARTUP_NOTE, g_fontSmall);

        AddControl(hwnd, L"BUTTON", L"Save",
                   BS_DEFPUSHBUTTON | WS_TABSTOP, 274, 448, 70, 28, IDOK,
                   g_fontUI);
        AddControl(hwnd, L"BUTTON", L"Cancel",
                   BS_PUSHBUTTON | WS_TABSTOP, 354, 448, 70, 28, IDCANCEL,
                   g_fontUI);
        return 0;

    case WM_COMMAND:
        OnCommand(LOWORD(wParam));
        return 0;

    case WM_HSCROLL:
        if ((HWND)lParam == GetDlgItem(hwnd, IDC_VOLUME)) {
            g_draft.volume = (int)SendMessageW(GetDlgItem(hwnd, IDC_VOLUME),
                                               TBM_GETPOS, 0, 0);
            UpdateVolumeLabel();
        }
        return 0;

    case WM_CTLCOLORSTATIC: {
        int id = GetDlgCtrlID((HWND)lParam);
        if (id == IDC_HINT || id == IDC_EFFECT_NOTE ||
            id == IDC_STARTUP_NOTE) {
            SetTextColor((HDC)wParam, RGB(96, 96, 96));
            SetBkMode((HDC)wParam, TRANSPARENT);
            return (LRESULT)(HBRUSH)(COLOR_BTNFACE + 1);
        }
        break;
    }

    case WM_CLOSE:
        HideDiscard();
        return 0;

    default:
        break;
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

static void CenterWindow(HWND hwnd)
{
    RECT r;
    RECT wa;
    int w;
    int h;
    int x;
    int y;

    GetWindowRect(hwnd, &r);
    w = r.right - r.left;
    h = r.bottom - r.top;
    SystemParametersInfoW(SPI_GETWORKAREA, 0, &wa, 0);
    x = wa.left + (wa.right - wa.left - w) / 2;
    y = wa.top + (wa.bottom - wa.top - h) / 2;
    SetWindowPos(hwnd, NULL, x, y, 0, 0,
                 SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
}

static BOOL CreateSettingsWindow(void)
{
    DWORD style = WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX;
    RECT rc = {0, 0, S(CLIENT_W), S(CLIENT_H)};

    g_fontUI = MakeFont(9, FW_NORMAL);
    g_fontSmall = MakeFont(8, FW_NORMAL);
    g_fontTitle = MakeFont(16, FW_SEMIBOLD);
    g_fontGroup = MakeFont(9, FW_SEMIBOLD);

    AdjustWindowRectEx(&rc, style, FALSE, WS_EX_CONTROLPARENT);
    g_win = CreateWindowExW(WS_EX_CONTROLPARENT, SETTINGS_CLASS, APP_TITLE,
                            style, CW_USEDEFAULT, CW_USEDEFAULT,
                            rc.right - rc.left, rc.bottom - rc.top, NULL, NULL,
                            g_hinst, NULL);
    if (!g_win) {
        UtilLog(L"CreateSettingsWindow failed (%lu)", GetLastError());
        return FALSE;
    }

    CenterWindow(g_win);
    return TRUE;
}

BOOL SettingsInit(HINSTANCE hinst)
{
    WNDCLASSEXW wc;

    ZeroMemory(&wc, sizeof(wc));
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = SettingsProc;
    wc.hInstance = hinst;
    wc.hCursor = LoadCursorW(NULL, (LPCWSTR)IDC_ARROW);
    wc.hIcon = LoadIconW(hinst, MAKEINTRESOURCEW(IDI_APP));
    wc.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);
    wc.lpszClassName = SETTINGS_CLASS;
    wc.style = CS_HREDRAW | CS_VREDRAW;
    return RegisterClassExW(&wc) != 0;
}

void SettingsShow(void)
{
    if (!g_win && !CreateSettingsWindow())
        return;

    if (IsWindowVisible(g_win)) {
        SetForegroundWindow(g_win);
        return;
    }

    ResetDraft();
    ShowWindow(g_win, SW_SHOWNORMAL);
    SetForegroundWindow(g_win);
    SetFocus(GetDlgItem(g_win, IDC_ENABLE));
}

HWND SettingsWindow(void)
{
    return g_win;
}

void SettingsShutdown(void)
{
    FreePick();
    if (g_win) {
        DestroyWindow(g_win);
        g_win = NULL;
    }
    if (g_fontUI) {
        DeleteObject(g_fontUI);
        g_fontUI = NULL;
    }
    if (g_fontSmall) {
        DeleteObject(g_fontSmall);
        g_fontSmall = NULL;
    }
    if (g_fontTitle) {
        DeleteObject(g_fontTitle);
        g_fontTitle = NULL;
    }
    if (g_fontGroup) {
        DeleteObject(g_fontGroup);
        g_fontGroup = NULL;
    }
}

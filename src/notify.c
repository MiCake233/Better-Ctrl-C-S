#include "notify.h"
#include "util.h"

#define TOAST_TIMER 1
#define FADE_IN_MS  110
#define FADE_OUT_MS 220

static HWND g_hwnd;
static HFONT g_font;
static wchar_t g_text[128];
static int g_w;
static int g_h;
static int g_alpha;
static int g_phase; /* 0 = fade in, 1 = hold, 2 = fade out */
static DWORD g_phaseStart;

static void SetAlpha(int alpha)
{
    if (alpha < 0)
        alpha = 0;
    if (alpha > 255)
        alpha = 255;
    g_alpha = alpha;
    SetLayeredWindowAttributes(g_hwnd, 0, (BYTE)alpha, LWA_ALPHA);
}

static void ComputeSize(void)
{
    HDC hdc;
    HGDIOBJ old;
    RECT rc;
    int pad;
    int icon;
    int gap;
    int textW;
    int w;

    pad = UtilScale(16);
    icon = UtilScale(22);
    gap = UtilScale(12);

    hdc = GetDC(g_hwnd);
    old = SelectObject(hdc, g_font);
    rc.left = 0;
    rc.top = 0;
    rc.right = 0;
    rc.bottom = 0;
    DrawTextW(hdc, g_text, -1, &rc,
              DT_CALCRECT | DT_SINGLELINE | DT_NOPREFIX);
    SelectObject(hdc, old);
    ReleaseDC(g_hwnd, hdc);

    textW = rc.right;
    w = pad + icon + gap + textW + pad;
    if (w < UtilScale(200))
        w = UtilScale(200);
    if (w > UtilScale(520))
        w = UtilScale(520);

    g_w = w;
    g_h = UtilScale(54);
}

static void PositionToast(void)
{
    HWND fg;
    HMONITOR mon;
    MONITORINFO mi;
    int margin;
    int x;
    int y;

    fg = GetForegroundWindow();
    mon = MonitorFromWindow(fg ? fg : GetDesktopWindow(),
                            MONITOR_DEFAULTTONEAREST);
    mi.cbSize = sizeof(mi);
    if (!GetMonitorInfoW(mon, &mi))
        return;

    margin = UtilScale(16);
    x = mi.rcWork.right - g_w - margin;
    y = mi.rcWork.bottom - g_h - margin;
    if (x < mi.rcWork.left)
        x = mi.rcWork.left;
    if (y < mi.rcWork.top)
        y = mi.rcWork.top;

    SetWindowPos(g_hwnd, HWND_TOPMOST, x, y, g_w, g_h,
                 SWP_NOACTIVATE | SWP_NOOWNERZORDER);
}

static void PaintToast(HWND hwnd)
{
    PAINTSTRUCT ps;
    HDC hdc;
    HDC mem;
    HBITMAP bmp;
    HBITMAP oldBmp;
    RECT rc;
    HBRUSH bg;
    HBRUSH accent;
    HPEN check;
    HPEN oldPen;
    HGDIOBJ oldBrush;
    HGDIOBJ oldFont;
    int pad;
    int icon;
    int gap;
    int cx;
    int cy;
    int r;
    int textLeft;
    int s;

    hdc = BeginPaint(hwnd, &ps);
    mem = CreateCompatibleDC(hdc);
    bmp = CreateCompatibleBitmap(hdc, g_w, g_h);
    oldBmp = (HBITMAP)SelectObject(mem, bmp);
    oldFont = SelectObject(mem, g_font);

    rc.left = 0;
    rc.top = 0;
    rc.right = g_w;
    rc.bottom = g_h;
    bg = CreateSolidBrush(RGB(28, 28, 30));
    FillRect(mem, &rc, bg);
    DeleteObject(bg);

    s = UtilScale(1);
    pad = UtilScale(16);
    icon = UtilScale(22);
    gap = UtilScale(12);
    cx = pad + icon / 2;
    cy = g_h / 2;
    r = icon / 2;

    accent = CreateSolidBrush(RGB(34, 197, 94));
    oldBrush = SelectObject(mem, accent);
    SelectObject(mem, GetStockObject(NULL_PEN));
    Ellipse(mem, cx - r, cy - r, cx + r + 1, cy + r + 1);
    SelectObject(mem, oldBrush);
    DeleteObject(accent);

    check = CreatePen(PS_SOLID, s >= 2 ? s : 2, RGB(255, 255, 255));
    oldPen = (HPEN)SelectObject(mem, check);
    MoveToEx(mem, cx - (r * 45) / 100, cy + (r * 5) / 100, NULL);
    LineTo(mem, cx - (r * 12) / 100, cy + (r * 38) / 100);
    LineTo(mem, cx + (r * 48) / 100, cy - (r * 34) / 100);
    SelectObject(mem, oldPen);
    DeleteObject(check);

    textLeft = pad + icon + gap;
    rc.left = textLeft;
    rc.right = g_w - pad;
    rc.top = 0;
    rc.bottom = g_h;
    SetBkMode(mem, TRANSPARENT);
    SetTextColor(mem, RGB(245, 245, 245));
    DrawTextW(mem, g_text, -1, &rc,
              DT_SINGLELINE | DT_VCENTER | DT_NOPREFIX | DT_END_ELLIPSIS);

    BitBlt(hdc, 0, 0, g_w, g_h, mem, 0, 0, SRCCOPY);

    SelectObject(mem, oldFont);
    SelectObject(mem, oldBmp);
    DeleteObject(bmp);
    DeleteDC(mem);
    EndPaint(hwnd, &ps);
}

static LRESULT CALLBACK ToastProc(HWND hwnd, UINT msg, WPARAM wParam,
                                  LPARAM lParam)
{
    switch (msg) {
    case WM_ERASEBKGND:
        return 1;

    case WM_NCHITTEST:
        return HTTRANSPARENT;

    case WM_PAINT:
        PaintToast(hwnd);
        return 0;

    case WM_TIMER:
        if (wParam == TOAST_TIMER) {
            DWORD elapsed = GetTickCount() - g_phaseStart;
            if (g_phase == 0) {
                int a = (int)(elapsed * 255 / FADE_IN_MS);
                if (a >= 255) {
                    SetAlpha(255);
                    g_phase = 1;
                    g_phaseStart = GetTickCount();
                } else {
                    SetAlpha(a);
                }
            } else if (g_phase == 1) {
                if (elapsed >= (DWORD)g_config.duration_ms) {
                    g_phase = 2;
                    g_phaseStart = GetTickCount();
                }
            } else {
                int a = 255 - (int)(elapsed * 255 / FADE_OUT_MS);
                if (a <= 0) {
                    KillTimer(hwnd, TOAST_TIMER);
                    ShowWindow(hwnd, SW_HIDE);
                } else {
                    SetAlpha(a);
                }
            }
        }
        return 0;

    default:
        return DefWindowProcW(hwnd, msg, wParam, lParam);
    }
}

BOOL NotifyInit(HINSTANCE hinst)
{
    WNDCLASSEXW wc;
    HDC hdc;

    ZeroMemory(&wc, sizeof(wc));
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = ToastProc;
    wc.hInstance = hinst;
    wc.hCursor = LoadCursorW(NULL, (LPCWSTR)IDC_ARROW);
    wc.lpszClassName = TOAST_CLASS;
    wc.style = CS_DROPSHADOW;
    if (!RegisterClassExW(&wc))
        return FALSE;

    hdc = GetDC(NULL);
    g_font = CreateFontW(-MulDiv(11, (int)g_dpi, 72), 0, 0, 0, FW_SEMIBOLD,
                         FALSE, FALSE, FALSE, DEFAULT_CHARSET,
                         OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                         CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE,
                         L"Segoe UI");
    ReleaseDC(NULL, hdc);
    if (!g_font)
        return FALSE;

    g_hwnd = CreateWindowExW(WS_EX_LAYERED | WS_EX_TOOLWINDOW |
                                 WS_EX_TOPMOST | WS_EX_NOACTIVATE,
                             TOAST_CLASS, L"", WS_POPUP, 0, 0, 10, 10, NULL,
                             NULL, hinst, NULL);
    if (!g_hwnd) {
        DeleteObject(g_font);
        g_font = NULL;
        return FALSE;
    }

    SetLayeredWindowAttributes(g_hwnd, 0, 0, LWA_ALPHA);
    return TRUE;
}

void NotifyShow(const wchar_t* text)
{
    HRGN rgn;
    BOOL visible;

    if (!g_hwnd)
        return;

    if (text && *text) {
        wcsncpy(g_text, text, 127);
        g_text[127] = 0;
    } else {
        wcscpy(g_text, L"Done");
    }

    ComputeSize();
    PositionToast();

    rgn = CreateRoundRectRgn(0, 0, g_w + 1, g_h + 1, UtilScale(10),
                             UtilScale(10));
    SetWindowRgn(g_hwnd, rgn, TRUE);

    visible = IsWindowVisible(g_hwnd);
    if (!visible) {
        SetAlpha(0);
        g_phase = 0;
        g_phaseStart = GetTickCount();
        ShowWindow(g_hwnd, SW_SHOWNOACTIVATE);
    } else {
        SetAlpha(255);
        g_phase = 1;
        g_phaseStart = GetTickCount();
    }

    SetTimer(g_hwnd, TOAST_TIMER, 15, NULL);
    InvalidateRect(g_hwnd, NULL, TRUE);
    UpdateWindow(g_hwnd);
}

void NotifyHide(void)
{
    if (!g_hwnd)
        return;
    KillTimer(g_hwnd, TOAST_TIMER);
    ShowWindow(g_hwnd, SW_HIDE);
}

void NotifyShutdown(void)
{
    if (g_hwnd) {
        KillTimer(g_hwnd, TOAST_TIMER);
        DestroyWindow(g_hwnd);
        g_hwnd = NULL;
    }
    if (g_font) {
        DeleteObject(g_font);
        g_font = NULL;
    }
}

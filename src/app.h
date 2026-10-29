/* Better Ctrl+C/S - shared application definitions. */
#ifndef APP_H
#define APP_H

#ifndef UNICODE
#define UNICODE
#endif
#ifndef _UNICODE
#define _UNICODE
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <windows.h>

#define APP_TITLE       L"Better Ctrl+C/S"
#define APP_ID          L"BetterCtrlCS"
#define APP_VERSION     L"1.0.0"

#define MAIN_CLASS      L"BetterCtrlCS.Main"
#define SETTINGS_CLASS  L"BetterCtrlCS.Settings"
#define TOAST_CLASS     L"BetterCtrlCS.Toast"

#define WM_APP_TRAY         (WM_APP + 1)
#define WM_APP_TRIGGER      (WM_APP + 2)
#define WM_APP_SHOWSETTINGS (WM_APP + 3)

#define FEEDBACK_SOUND  0x01
#define FEEDBACK_NOTIFY 0x02

typedef struct {
    BOOL    enabled;        /* master switch */
    BOOL    c_enabled;      /* Ctrl+C indicator enabled */
    BOOL    s_enabled;      /* Ctrl+S indicator enabled */
    int     c_feedback;     /* FEEDBACK_* flags for Ctrl+C */
    int     s_feedback;     /* FEEDBACK_* flags for Ctrl+S */
    int     volume;         /* 0..100 */
    BOOL    custom_sound;   /* use custom.wav instead of the built-in tone */
    wchar_t text_copy[96];
    wchar_t text_save[96];
    int     duration_ms;    /* notification visible time */
} Config;

extern Config    g_config;
extern HINSTANCE g_hinst;
extern HWND      g_hwndMain;
extern UINT      g_dpi;

void AppUpdateTray(void);
void AppRequestExit(void);

#endif /* APP_H */

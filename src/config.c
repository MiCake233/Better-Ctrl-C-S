#include "config.h"
#include "util.h"

#include <stdarg.h>
#include <wchar.h>

static BOOL ParseBool(const wchar_t* v, BOOL def)
{
    if (!_wcsicmp(v, L"1") || !_wcsicmp(v, L"true") ||
        !_wcsicmp(v, L"yes") || !_wcsicmp(v, L"on"))
        return TRUE;
    if (!_wcsicmp(v, L"0") || !_wcsicmp(v, L"false") ||
        !_wcsicmp(v, L"no") || !_wcsicmp(v, L"off"))
        return FALSE;
    return def;
}

static int ParseInt(const wchar_t* v, int def)
{
    wchar_t* end = NULL;
    long n = wcstol(v, &end, 10);
    if (end == v || n < -1000000L || n > 1000000L)
        return def;
    return (int)n;
}

/* Returns FEEDBACK_* flags, 0 for "none", or -1 when unrecognized. */
static int ParseFeedback(const wchar_t* v)
{
    if (!_wcsicmp(v, L"sound") || !_wcsicmp(v, L"beep"))
        return FEEDBACK_SOUND;
    if (!_wcsicmp(v, L"notification") || !_wcsicmp(v, L"popup"))
        return FEEDBACK_NOTIFY;
    if (!_wcsicmp(v, L"both"))
        return FEEDBACK_SOUND | FEEDBACK_NOTIFY;
    if (!_wcsicmp(v, L"none") || !_wcsicmp(v, L"off"))
        return 0;
    return -1;
}

void ConfigSetDefaults(Config* c)
{
    BOOL zh = PRIMARYLANGID(GetUserDefaultUILanguage()) == LANG_CHINESE;

    ZeroMemory(c, sizeof(*c));
    c->enabled = TRUE;
    c->c_enabled = TRUE;
    c->s_enabled = TRUE;
    c->c_feedback = FEEDBACK_SOUND;
    c->s_feedback = FEEDBACK_NOTIFY;
    c->volume = 60;
    c->custom_sound = FALSE;
    c->duration_ms = 1500;
    wcscpy(c->text_copy, zh ? L"\u590d\u5236\u6210\u529f" : L"Copied");
    wcscpy(c->text_save, zh ? L"\u4fdd\u5b58\u6210\u529f" : L"Saved");
}

static void ApplyKey(Config* c, const wchar_t* section, const wchar_t* key,
                     const wchar_t* value)
{
    int f;

    if (!_wcsicmp(section, L"General")) {
        if (!_wcsicmp(key, L"Enabled")) {
            c->enabled = ParseBool(value, c->enabled);
        } else if (!_wcsicmp(key, L"CtrlC")) {
            c->c_enabled = ParseBool(value, c->c_enabled);
        } else if (!_wcsicmp(key, L"CtrlS")) {
            c->s_enabled = ParseBool(value, c->s_enabled);
        } else if (!_wcsicmp(key, L"CtrlCFeedback")) {
            f = ParseFeedback(value);
            if (f >= 0)
                c->c_feedback = f;
        } else if (!_wcsicmp(key, L"CtrlSFeedback")) {
            f = ParseFeedback(value);
            if (f >= 0)
                c->s_feedback = f;
        }
    } else if (!_wcsicmp(section, L"Sound")) {
        if (!_wcsicmp(key, L"Volume")) {
            c->volume = ParseInt(value, c->volume);
        } else if (!_wcsicmp(key, L"CustomSound")) {
            c->custom_sound = ParseBool(value, c->custom_sound);
        }
    } else if (!_wcsicmp(section, L"Notification")) {
        if (!_wcsicmp(key, L"TextCopy")) {
            if (*value) {
                wcsncpy(c->text_copy, value, 95);
                c->text_copy[95] = 0;
            }
        } else if (!_wcsicmp(key, L"TextSave")) {
            if (*value) {
                wcsncpy(c->text_save, value, 95);
                c->text_save[95] = 0;
            }
        } else if (!_wcsicmp(key, L"DurationMs")) {
            c->duration_ms = ParseInt(value, c->duration_ms);
        }
    }
}

static void Sanitize(Config* c)
{
    if (c->volume < 0)
        c->volume = 0;
    if (c->volume > 100)
        c->volume = 100;
    if (c->duration_ms < 300)
        c->duration_ms = 300;
    if (c->duration_ms > 10000)
        c->duration_ms = 10000;
    if (c->c_feedback == 0) {
        c->c_enabled = FALSE;
        c->c_feedback = FEEDBACK_SOUND;
    }
    if (c->s_feedback == 0) {
        c->s_enabled = FALSE;
        c->s_feedback = FEEDBACK_SOUND;
    }
    if (!c->text_copy[0])
        wcscpy(c->text_copy, L"Copied");
    if (!c->text_save[0])
        wcscpy(c->text_save, L"Saved");
}

BOOL ConfigLoad(Config* c, const wchar_t* path)
{
    BYTE* raw = NULL;
    DWORD size = 0;
    wchar_t* text = NULL;
    wchar_t* end;
    wchar_t* p;
    wchar_t section[32] = L"";
    int wlen = 0;

    ConfigSetDefaults(c);

    if (!UtilReadFile(path, 256 * 1024, &raw, &size))
        return FALSE;

    if (size >= 2 && raw[0] == 0xFF && raw[1] == 0xFE) {
        wlen = (int)((size - 2) / 2);
        text = (wchar_t*)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY,
                                   ((SIZE_T)wlen + 1) * sizeof(wchar_t));
        if (text)
            memcpy(text, raw + 2, (SIZE_T)wlen * sizeof(wchar_t));
    } else {
        DWORD off = 0;
        UINT cp = CP_UTF8;
        DWORD flags = MB_ERR_INVALID_CHARS;

        if (size >= 3 && raw[0] == 0xEF && raw[1] == 0xBB && raw[2] == 0xBF)
            off = 3;

        wlen = MultiByteToWideChar(CP_UTF8, flags, (const char*)raw + off,
                                   (int)(size - off), NULL, 0);
        if (wlen <= 0) {
            cp = CP_ACP;
            flags = 0;
            wlen = MultiByteToWideChar(CP_ACP, 0, (const char*)raw + off,
                                       (int)(size - off), NULL, 0);
        }
        if (wlen > 0) {
            text = (wchar_t*)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY,
                                       ((SIZE_T)wlen + 1) * sizeof(wchar_t));
            if (text)
                MultiByteToWideChar(cp, flags, (const char*)raw + off,
                                    (int)(size - off), text, wlen);
        }
    }

    HeapFree(GetProcessHeap(), 0, raw);

    if (!text) {
        UtilLog(L"ConfigLoad: cannot decode %s", path);
        return FALSE;
    }
    text[wlen] = 0;
    end = text + wlen;

    p = text;
    while (p && p < end) {
        wchar_t* next = wcschr(p, L'\n');
        if (next)
            *next = 0;
        UtilTrim(p);

        if (*p && *p != L';' && *p != L'#') {
            if (*p == L'[') {
                wchar_t* close = wcschr(p, L']');
                if (close) {
                    wchar_t* name = p + 1;
                    *close = 0;
                    UtilTrim(name);
                    wcsncpy(section, name, 31);
                    section[31] = 0;
                }
            } else {
                wchar_t* eq = wcschr(p, L'=');
                if (eq) {
                    wchar_t* key = p;
                    wchar_t* value = eq + 1;
                    *eq = 0;
                    UtilTrim(key);
                    UtilTrim(value);
                    if (*key)
                        ApplyKey(c, section, key, value);
                }
            }
        }

        if (!next)
            break;
        p = next + 1;
    }

    HeapFree(GetProcessHeap(), 0, text);
    Sanitize(c);
    return TRUE;
}

typedef struct {
    wchar_t* p;
    size_t left;
} Writer;

static void Wr(Writer* w, const wchar_t* fmt, ...)
{
    wchar_t tmp[512];
    size_t n;
    va_list ap;

    va_start(ap, fmt);
    wvsprintfW(tmp, fmt, ap);
    va_end(ap);
    tmp[511] = 0;

    n = wcslen(tmp);
    if (n >= w->left)
        n = w->left - 1;
    wcsncpy(w->p, tmp, n);
    w->p += n;
    w->left -= n;
    *w->p = 0;
}

static const wchar_t* FeedbackName(int f)
{
    if ((f & (FEEDBACK_SOUND | FEEDBACK_NOTIFY)) ==
        (FEEDBACK_SOUND | FEEDBACK_NOTIFY))
        return L"both";
    if (f & FEEDBACK_NOTIFY)
        return L"notification";
    return L"sound";
}

BOOL ConfigSave(const Config* c, const wchar_t* path)
{
    wchar_t buf[4096];
    Writer w;
    char* utf8;
    int cb;
    BOOL ok;

    w.p = buf;
    w.left = 4096;
    *buf = 0;

    Wr(&w, L"; Better Ctrl+C/S settings\r\n");
    Wr(&w, L"; This file is rewritten by the application.\r\n");
    Wr(&w, L"; Manual edits take effect after restarting the application.\r\n\r\n");
    Wr(&w, L"[General]\r\n");
    Wr(&w, L"Enabled=%d\r\n", c->enabled ? 1 : 0);
    Wr(&w, L"CtrlC=%d\r\n", c->c_enabled ? 1 : 0);
    Wr(&w, L"CtrlS=%d\r\n", c->s_enabled ? 1 : 0);
    Wr(&w, L"CtrlCFeedback=%s\r\n", FeedbackName(c->c_feedback));
    Wr(&w, L"CtrlSFeedback=%s\r\n\r\n", FeedbackName(c->s_feedback));
    Wr(&w, L"[Sound]\r\n");
    Wr(&w, L"Volume=%d\r\n", c->volume);
    Wr(&w, L"CustomSound=%d\r\n\r\n", c->custom_sound ? 1 : 0);
    Wr(&w, L"[Notification]\r\n");
    Wr(&w, L"TextCopy=%s\r\n", c->text_copy);
    Wr(&w, L"TextSave=%s\r\n", c->text_save);
    Wr(&w, L"DurationMs=%d\r\n", c->duration_ms);

    cb = WideCharToMultiByte(CP_UTF8, 0, buf, -1, NULL, 0, NULL, NULL);
    if (cb <= 0)
        return FALSE;

    utf8 = (char*)HeapAlloc(GetProcessHeap(), 0, (SIZE_T)cb);
    if (!utf8)
        return FALSE;

    WideCharToMultiByte(CP_UTF8, 0, buf, -1, utf8, cb, NULL, NULL);
    ok = UtilWriteFileAtomic(path, utf8, (DWORD)(cb - 1));
    HeapFree(GetProcessHeap(), 0, utf8);

    if (!ok)
        UtilLog(L"ConfigSave: write failed (%lu)", GetLastError());
    return ok;
}

#include "util.h"

#include <shlobj.h>
#include <stdarg.h>
#include <string.h>

#define RUN_KEY L"Software\\Microsoft\\Windows\\CurrentVersion\\Run"
#define LOG_MAX_BYTES (256u * 1024u)

wchar_t g_exePath[MAX_PATH];
wchar_t g_dataDir[MAX_PATH];
wchar_t g_iniPath[MAX_PATH];
wchar_t g_customWavPath[MAX_PATH];
wchar_t g_logPath[MAX_PATH];

BOOL UtilEnsureDir(const wchar_t* dir)
{
    DWORD attr = GetFileAttributesW(dir);
    if (attr != INVALID_FILE_ATTRIBUTES)
        return (attr & FILE_ATTRIBUTE_DIRECTORY) != 0;
    if (CreateDirectoryW(dir, NULL))
        return TRUE;
    return GetLastError() == ERROR_ALREADY_EXISTS;
}

BOOL UtilFileExists(const wchar_t* path)
{
    DWORD attr = GetFileAttributesW(path);
    return attr != INVALID_FILE_ATTRIBUTES && !(attr & FILE_ATTRIBUTE_DIRECTORY);
}

BOOL UtilInitPaths(void)
{
    wchar_t appdata[MAX_PATH] = L"";
    BOOL ok = FALSE;

    if (GetModuleFileNameW(NULL, g_exePath, MAX_PATH) == 0)
        return FALSE;

    if (SUCCEEDED(SHGetFolderPathW(NULL, CSIDL_APPDATA | CSIDL_FLAG_CREATE, NULL,
                                   SHGFP_TYPE_CURRENT, appdata))) {
        wsprintfW(g_dataDir, L"%s\\%s", appdata, APP_ID);
        ok = UtilEnsureDir(g_dataDir);
    }

    if (!ok) {
        /* Fall back to the executable directory when %APPDATA% is unusable. */
        wchar_t* slash;
        wcscpy(g_dataDir, g_exePath);
        slash = wcsrchr(g_dataDir, L'\\');
        if (slash) {
            *slash = 0;
        } else {
            wcscpy(g_dataDir, L".");
        }
        ok = UtilEnsureDir(g_dataDir);
        if (!ok)
            return FALSE;
    }

    wsprintfW(g_iniPath, L"%s\\config.ini", g_dataDir);
    wsprintfW(g_customWavPath, L"%s\\custom.wav", g_dataDir);
    wsprintfW(g_logPath, L"%s\\BetterCtrlCS.log", g_dataDir);
    return TRUE;
}

BOOL UtilReadFile(const wchar_t* path, DWORD maxBytes, BYTE** data, DWORD* size)
{
    HANDLE h;
    LARGE_INTEGER fs;
    BYTE* buf;
    DWORD total = 0;

    *data = NULL;
    *size = 0;

    h = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL,
                    OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (h == INVALID_HANDLE_VALUE)
        return FALSE;

    if (!GetFileSizeEx(h, &fs) || fs.QuadPart <= 0 ||
        fs.QuadPart > (LONGLONG)maxBytes) {
        CloseHandle(h);
        return FALSE;
    }

    buf = (BYTE*)HeapAlloc(GetProcessHeap(), 0, (SIZE_T)fs.QuadPart + 1);
    if (!buf) {
        CloseHandle(h);
        return FALSE;
    }

    while (total < (DWORD)fs.QuadPart) {
        DWORD got = 0;
        if (!ReadFile(h, buf + total, (DWORD)fs.QuadPart - total, &got, NULL) ||
            got == 0) {
            break;
        }
        total += got;
    }
    CloseHandle(h);

    if (total != (DWORD)fs.QuadPart) {
        HeapFree(GetProcessHeap(), 0, buf);
        return FALSE;
    }

    buf[total] = 0;
    *data = buf;
    *size = total;
    return TRUE;
}

BOOL UtilWriteFileAtomic(const wchar_t* path, const void* data, DWORD size)
{
    wchar_t tmp[MAX_PATH + 8];
    HANDLE h;
    DWORD total = 0;
    int attempt;
    BOOL moved = FALSE;

    if (wcslen(path) + 5 >= MAX_PATH + 8)
        return FALSE;
    wsprintfW(tmp, L"%s.tmp", path);

    h = CreateFileW(tmp, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS,
                    FILE_ATTRIBUTE_NORMAL, NULL);
    if (h == INVALID_HANDLE_VALUE)
        return FALSE;

    while (total < size) {
        DWORD wrote = 0;
        if (!WriteFile(h, (const BYTE*)data + total, size - total, &wrote, NULL) ||
            wrote == 0) {
            break;
        }
        total += wrote;
    }
    FlushFileBuffers(h);
    CloseHandle(h);

    if (total != size) {
        DeleteFileW(tmp);
        return FALSE;
    }

    for (attempt = 0; attempt < 3 && !moved; attempt++) {
        if (MoveFileExW(tmp, path, MOVEFILE_REPLACE_EXISTING |
                                       MOVEFILE_WRITE_THROUGH)) {
            moved = TRUE;
        } else {
            Sleep(30);
        }
    }

    if (!moved)
        DeleteFileW(tmp);
    return moved;
}

BOOL UtilUtf8ToWide(const char* src, int srcLen, wchar_t* dst, int dstCch)
{
    int n;
    if (srcLen < 0)
        srcLen = (int)strlen(src);
    if (srcLen == 0) {
        if (dstCch > 0)
            dst[0] = 0;
        return TRUE;
    }
    if (dstCch <= 0)
        return FALSE;

    n = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, src, srcLen, dst,
                            dstCch - 1);
    if (n <= 0)
        return FALSE;
    dst[n] = 0;
    return TRUE;
}

BOOL UtilWideToUtf8(const wchar_t* src, char* dst, int dstCb)
{
    int n;
    if (dstCb <= 0)
        return FALSE;
    n = WideCharToMultiByte(CP_UTF8, 0, src, -1, dst, dstCb - 1, NULL, NULL);
    if (n <= 0)
        return FALSE;
    dst[n] = 0;
    return TRUE;
}

void UtilTrim(wchar_t* s)
{
    wchar_t* p = s;
    size_t n;

    while (*p == L' ' || *p == L'\t')
        p++;
    if (p != s)
        memmove(s, p, (wcslen(p) + 1) * sizeof(wchar_t));

    n = wcslen(s);
    while (n > 0) {
        wchar_t c = s[n - 1];
        if (c != L' ' && c != L'\t' && c != L'\r' && c != L'\n')
            break;
        s[--n] = 0;
    }
}

void UtilLog(const wchar_t* fmt, ...)
{
    wchar_t msg[1024];
    wchar_t line[1200];
    char utf8[4096];
    SYSTEMTIME st;
    va_list ap;
    HANDLE h;
    DWORD wrote = 0;
    int cb;
    WIN32_FILE_ATTRIBUTE_DATA fad;

    if (!g_logPath[0])
        return;

    va_start(ap, fmt);
    wvsprintfW(msg, fmt, ap);
    va_end(ap);
    msg[1023] = 0;

    GetLocalTime(&st);
    wsprintfW(line, L"[%04u-%02u-%02u %02u:%02u:%02u] %s\r\n",
              st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond,
              msg);

    cb = WideCharToMultiByte(CP_UTF8, 0, line, -1, utf8, (int)sizeof(utf8) - 1,
                             NULL, NULL);
    if (cb <= 0)
        return;

    if (GetFileAttributesExW(g_logPath, GetFileExInfoStandard, &fad)) {
        ULONGLONG size = ((ULONGLONG)fad.nFileSizeHigh << 32) | fad.nFileSizeLow;
        if (size > LOG_MAX_BYTES)
            DeleteFileW(g_logPath);
    }

    h = CreateFileW(g_logPath, FILE_APPEND_DATA,
                    FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_ALWAYS,
                    FILE_ATTRIBUTE_NORMAL, NULL);
    if (h == INVALID_HANDLE_VALUE)
        return;

    WriteFile(h, utf8, (DWORD)(cb - 1), &wrote, NULL);
    CloseHandle(h);
}

BOOL UtilAutostartEnabled(void)
{
    wchar_t value[MAX_PATH + 64];
    DWORD cb = (DWORD)sizeof(value);

    return RegGetValueW(HKEY_CURRENT_USER, RUN_KEY, APP_ID, RRF_RT_REG_SZ, NULL,
                        value, &cb) == ERROR_SUCCESS;
}

BOOL UtilAutostartIsCurrent(void)
{
    wchar_t value[MAX_PATH + 64];
    wchar_t expected[MAX_PATH + 64];
    DWORD cb = (DWORD)sizeof(value);

    if (RegGetValueW(HKEY_CURRENT_USER, RUN_KEY, APP_ID, RRF_RT_REG_SZ, NULL,
                     value, &cb) != ERROR_SUCCESS)
        return FALSE;

    wsprintfW(expected, L"\"%s\" --background", g_exePath);
    return _wcsicmp(value, expected) == 0;
}

BOOL UtilAutostartSet(BOOL enable)
{
    HKEY key;
    LONG r;
    BOOL ok;

    r = RegCreateKeyExW(HKEY_CURRENT_USER, RUN_KEY, 0, NULL, 0, KEY_SET_VALUE,
                        NULL, &key, NULL);
    if (r != ERROR_SUCCESS)
        return FALSE;

    if (enable) {
        wchar_t cmd[MAX_PATH + 64];
        wsprintfW(cmd, L"\"%s\" --background", g_exePath);
        r = RegSetValueExW(key, APP_ID, 0, REG_SZ, (const BYTE*)cmd,
                           (DWORD)((wcslen(cmd) + 1) * sizeof(wchar_t)));
    } else {
        r = RegDeleteValueW(key, APP_ID);
        if (r == ERROR_FILE_NOT_FOUND)
            r = ERROR_SUCCESS;
    }

    ok = (r == ERROR_SUCCESS);
    RegCloseKey(key);
    return ok;
}

int UtilScale(int dip)
{
    if (g_dpi == 0)
        return dip;
    return MulDiv(dip, (int)g_dpi, 96);
}

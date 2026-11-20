/* Paths, files, UTF conversion, logging, and HKCU autostart helpers. */
#ifndef UTIL_H
#define UTIL_H

#include "app.h"

/* Long enough for the longest path SHGetFolderPathW can return (MAX_PATH-1)
   plus the directory name and the longest file we store under it. */
#define APP_PATH_CCH 512

extern wchar_t g_exePath[MAX_PATH];
extern wchar_t g_dataDir[APP_PATH_CCH];
extern wchar_t g_iniPath[APP_PATH_CCH];
extern wchar_t g_customWavPath[APP_PATH_CCH];
extern wchar_t g_logPath[APP_PATH_CCH];

BOOL UtilInitPaths(void);
BOOL UtilJoinPath(wchar_t* dst, int dstCch, const wchar_t* dir,
                  const wchar_t* name);
BOOL UtilEnsureDir(const wchar_t* dir);
BOOL UtilFileExists(const wchar_t* path);
BOOL UtilReadFile(const wchar_t* path, DWORD maxBytes, BYTE** data, DWORD* size);
BOOL UtilWriteFileAtomic(const wchar_t* path, const void* data, DWORD size);
BOOL UtilUtf8ToWide(const char* src, int srcLen, wchar_t* dst, int dstCch);
BOOL UtilWideToUtf8(const wchar_t* src, char* dst, int dstCb);
void UtilTrim(wchar_t* s);
void UtilLog(const wchar_t* fmt, ...);

BOOL UtilAutostartEnabled(void);
BOOL UtilAutostartIsCurrent(void);
BOOL UtilAutostartSet(BOOL enable);

int UtilScale(int dip);

#endif /* UTIL_H */

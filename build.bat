@echo off
setlocal
pushd "%~dp0"

where gcc >nul 2>nul
if errorlevel 1 (
    echo [error] gcc was not found in PATH. Install MinGW-w64 first.
    popd
    exit /b 1
)
where windres >nul 2>nul
if errorlevel 1 (
    echo [error] windres was not found in PATH. Install MinGW-w64 first.
    popd
    exit /b 1
)

if not exist build mkdir build

windres -I src -I resources resources\app.rc -O coff -o build\app.res
if errorlevel 1 goto fail

gcc -c src\manifest_stub.c -o build\default-manifest.o
if errorlevel 1 goto fail

gcc -std=c11 -Os -s -mwindows -Wall -Wextra ^
    -DUNICODE -D_UNICODE -DWIN32_LEAN_AND_MEAN -DNOMINMAX ^
    -ffunction-sections -fdata-sections -B build ^
    src\main.c src\config.c src\audio.c src\notify.c src\settings.c src\util.c ^
    build\app.res -o build\BetterCtrlCS.exe ^
    -Wl,--gc-sections -static-libgcc ^
    -luser32 -lgdi32 -lshell32 -ladvapi32 -lcomctl32 -lcomdlg32 -lwinmm
if errorlevel 1 goto fail

echo Built build\BetterCtrlCS.exe
popd
exit /b 0

:fail
echo [error] Build failed.
popd
exit /b 1

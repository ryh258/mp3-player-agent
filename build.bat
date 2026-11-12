@echo off
rem ============================================================
rem  CQ Music Player - one-click build script
rem
rem  Requires MinGW-w64 (gcc + windres). Pick one:
rem    1) put MinGW's bin folder on PATH, or
rem    2) set MINGW below to your MinGW root, or
rem    3) drop a portable mingw64\ folder next to this script.
rem  When all of them are absent the script probes common installs.
rem
rem  NOTE: this file is intentionally pure ASCII so that it runs on
rem  every Windows code page and renders correctly on GitHub.
rem ============================================================
setlocal
cd /d "%~dp0"

rem ---- 1. locate the compiler -------------------------------
rem  Example:  set "MINGW=D:\mingw64"
set "MINGW="
set "BINDIR="

if not defined BINDIR if exist "%~dp0mingw64\bin\gcc.exe" set "BINDIR=%~dp0mingw64\bin"
if not defined BINDIR (
    for %%C in (gcc.exe) do if not "%%~$PATH:C"=="" for %%D in ("%%~$PATH:C") do set "BINDIR=%%~dpD"
)
if not defined BINDIR if defined MINGW if exist "%MINGW%\bin\gcc.exe" set "BINDIR=%MINGW%\bin"
if not defined BINDIR if exist "C:\msys64\ucrt64\bin\gcc.exe"  set "BINDIR=C:\msys64\ucrt64\bin"
if not defined BINDIR if exist "C:\msys64\mingw64\bin\gcc.exe" set "BINDIR=C:\msys64\mingw64\bin"
if not defined BINDIR if exist "C:\msys64\clang64\bin\gcc.exe" set "BINDIR=C:\msys64\clang64\bin"
if not defined BINDIR if exist "C:\mingw64\bin\gcc.exe"        set "BINDIR=C:\mingw64\bin"
if not defined BINDIR if exist "C:\mingw32\bin\gcc.exe"        set "BINDIR=C:\mingw32\bin"

if not defined BINDIR goto :nogcc
set "PATH=%BINDIR%;%PATH%"

for /f "delims=" %%V in ('gcc -dumpversion') do set "GCCVER=%%V"
echo   compiler: %BINDIR%gcc.exe  [GCC %GCCVER%]

rem ---- 2. compile resources ---------------------------------
if not exist build mkdir build
echo [1/3] compiling resources (icon + manifest + dialog)...
windres res\app.rc -o build\app_res.o -I res
if errorlevel 1 goto :err

rem ---- 3. compile and link ----------------------------------
echo [2/3] compiling and linking...
rem  -Wl,--no-insert-timestamp drops the PE timestamp so that the
rem  same source tree produces a byte-identical binary.
gcc -O2 -g -Wall -Wextra -municode -mwindows -D_WIN32_WINNT=0x0601 ^
    -finput-charset=UTF-8 -fexec-charset=UTF-8 -Wl,--no-insert-timestamp ^
    src\main.c src\ui.c src\app.c src\playlist.c src\util.c ^
    src\tag_id3.c src\media_len.c src\store.c src\lrc.c ^
    src\theme.c src\cover.c src\wicdec.c src\nowplaying.c src\splash.c ^
    src\nav.c src\lyricview.c src\appvol.c src\crash.c ^
    src\selftest.c src\audio_mf.c ^
    build\app_res.o ^
    -o MP3Player.exe ^
    -lcomctl32 -lwinmm -lgdi32 -luser32 -lole32 -lshell32 -lcomdlg32 ^
    -lmfplat -lmfreadwrite -lmfuuid -loleaut32 -luuid -lmsimg32 ^
    -lwindowscodecs -lshlwapi
if errorlevel 1 goto :err

echo [3/3] build succeeded
echo.
echo    binary  : %~dp0MP3Player.exe
echo    selftest: MP3Player.exe --selftest
echo.
call :pause_if_dblclick %1
exit /b 0

:nogcc
echo.
echo  [ERROR] gcc.exe not found
echo.
echo  Install MinGW-w64 first, then do one of the following:
echo    1) add MinGW's bin folder to PATH and reopen the terminal
echo    2) open this file in Notepad and set MINGW= to your MinGW root
echo    3) copy a portable mingw64\ folder next to this script
echo.
echo  Download: https://winlibs.com/   or   https://www.msys2.org/
echo.
goto :err

:err
echo  *** BUILD FAILED ***
echo.
call :pause_if_dblclick %1
exit /b 1

:pause_if_dblclick
rem  Pause before the window closes so a double-click user can read the
rem  result. Pass --no-pause (or set CI=1) for non-interactive runs.
if /i "%~1"=="--no-pause" goto :eof
if defined CI goto :eof
pause
goto :eof

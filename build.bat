@echo off
rem ============================================================
rem  重庆大工程music  Alpha v0.3  —— 一键编译脚本
rem ============================================================
setlocal
chcp 936 >nul

set "MINGW=E:\小熊猫C语言\mingw64"
if not exist "%MINGW%\bin\gcc.exe" (
    echo [错误] 找不到编译器: %MINGW%\bin\gcc.exe
    echo        请修改本脚本顶部的 MINGW 变量。
    pause
    exit /b 1
)
set "PATH=%MINGW%\bin;%PATH%"

cd /d "%~dp0"
if not exist build mkdir build

echo [1/4] 编译资源（图标 + 清单 + 输入对话框）...
windres res\app.rc -o build\app_res.o -I res
if errorlevel 1 goto err

echo [2/3] 编译并链接主程序...
rem  -Wl,--no-insert-timestamp  去掉 PE 头时间戳，让构建逐字节可重复
gcc -O2 -g -Wall -Wextra -municode -mwindows -D_WIN32_WINNT=0x0601 ^
    -finput-charset=UTF-8 -fexec-charset=UTF-8 -Wl,--no-insert-timestamp ^
    src\main.c src\ui.c src\app.c src\playlist.c src\util.c ^
    src\tag_id3.c src\media_len.c src\store.c src\lrc.c ^
    src\theme.c src\cover.c src\wicdec.c src\nowplaying.c src\splash.c src\nav.c src\lyricview.c src\appvol.c src\crash.c ^
    src\selftest.c src\audio_mf.c ^
    build\app_res.o ^
    -o MP3Player.exe ^
    -lcomctl32 -lwinmm -lgdi32 -luser32 -lole32 -lshell32 -lcomdlg32 ^
    -lmfplat -lmfreadwrite -lmfuuid -loleaut32 -luuid -lmsimg32 -lwindowscodecs -lshlwapi
if errorlevel 1 goto err

echo [3/3] 编译成功！
echo.
echo    可执行文件 : %~dp0MP3Player.exe
echo    音频自检   : MP3Player.exe --selftest
echo.
pause
exit /b 0

:err
echo.
echo *** 编译失败 ***
pause
exit /b 1

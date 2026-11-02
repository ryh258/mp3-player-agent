/* splash.h —— 启动页 */
#ifndef SPLASH_H
#define SPLASH_H

#include <windows.h>

#define SPLASH_CLASS L"Mp3SplashWnd"

/* 居中显示启动页 ms 毫秒（点一下或按键可提前跳过） */
void splash_show(HINSTANCE hInst, int dpi, int ms);

#endif /* SPLASH_H */

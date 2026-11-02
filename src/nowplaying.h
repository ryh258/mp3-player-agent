/* nowplaying.h —— 正在播放页（自绘黑胶唱片） */
#ifndef NOWPLAYING_H
#define NOWPLAYING_H

#include <windows.h>

#define NOWPLAYING_CLASS L"Mp3NowPlaying"

void nowplaying_register_class(HINSTANCE hi);
struct App;

void nowplaying_set_app(HWND h, struct App *app);

#endif /* NOWPLAYING_H */

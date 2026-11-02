/* nav.h —— 左侧导航栏（自绘）
 *
 * 用自绘而不是 ListBox：默认 ListBox 的样子太老，
 * 自绘才能做出"选中项浮起 + 强调色图标"的现代观感。
 *
 * 点击条目时向父窗口发：
 *     WM_COMMAND, LOWORD(wParam) = 条目索引, HIWORD(wParam) = NAV_NOTIFY
 */
#ifndef NAV_H
#define NAV_H

#include <windows.h>

#define NAV_CLASS   L"Mp3NavBar"
#define NAV_NOTIFY  0x0900        /* 自定义通知码 */

enum {
    NAV_ICON_NONE = 0,
    NAV_ICON_SONG,       /* 音符 */
    NAV_ICON_LIST,       /* 列表 */
    NAV_ICON_CLOCK,      /* 时钟 */
    NAV_ICON_PLAYLIST,   /* 小列表（二级项） */
    NAV_ICON_MIC,        /* 话筒（歌手） */
    NAV_ICON_DISC,       /* 唱片（专辑） */
    NAV_ICON_HEART,      /* 心形（我喜欢） */
    NAV_ICON_PLAY        /* 播放三角（正在播放） */
};

#define NAV_TEXT_MAX 128
#define NAV_MAX_ITEMS 64

typedef struct NavItem {
    wchar_t text[NAV_TEXT_MAX];
    int     level;    /* 0 = 一级，1 = 二级（缩进） */
    int     icon;     /* NAV_ICON_* */
    int     gap;      /* 该项前面留一点空隙（分组用） */
} NavItem;

void nav_register_class(HINSTANCE hInst);
void nav_set_items(HWND h, const NavItem *items, int n);
void nav_set_sel(HWND h, int sel);
int  nav_get_sel(HWND h);
void nav_set_font(HWND h, HFONT f);
void nav_set_dpi(HWND h, int dpi);

#endif /* NAV_H */

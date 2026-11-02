/* lyricview.h —— 右侧歌词面板（自绘）
 *
 * 功能：按时间高亮当前行、自动滚动、点击某一行跳到对应时间。
 *
 * 注意：控件只持有 Lrc 的指针，不复制内容。
 *       调用方必须保证 set_lrc 传进来的 Lrc 在控件使用期间一直有效
 *       （App 里由 g_app.lrc 持有；换歌时先 set_lrc(NULL) 再释放旧的）。
 */
#ifndef LYRICVIEW_H
#define LYRICVIEW_H

#include <windows.h>
#include "lrc.h"

#define LYRICVIEW_CLASS  L"Mp3LyricView"
#define LYRIC_NOTIFY     0x0901   /* 点击第 i 行 -> WM_COMMAND, LOWORD(wParam)=i */

void lyricview_register_class(HINSTANCE hInst);

/* lrc 传 NULL 表示当前没有歌词，面板显示提示文字 */
void lyricview_set_lrc(HWND h, const Lrc *lrc, const wchar_t *songTitle);

/* 更新时间（毫秒），控件据此决定高亮哪一行并滚动 */
void lyricview_set_time(HWND h, int ms);

void lyricview_set_font(HWND h, HFONT base);
void lyricview_set_dpi(HWND h, int dpi);
void lyricview_set_colors(HWND h, COLORREF bg, COLORREF text, COLORREF cur, COLORREF accent);

#endif /* LYRICVIEW_H */

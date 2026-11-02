/* theme.h —— 统一配色（按用户提供的设计稿：深蓝 + 白）
 *
 * 视觉规则（照设计稿）：
 *   外壳（侧栏 / 顶部工具栏）= 深蓝，文字图标全白
 *   内容区（歌曲列表）        = 浅底，深色文字
 *   全屏页（正在播放 / 我的）  = 深蓝渐变
 */
#ifndef THEME_H
#define THEME_H

#include <windows.h>

/* ---- 深蓝外壳 ---- */
#define TH_NAV_TOP    RGB( 14,  58, 108)   /* 侧栏渐变上端 */
#define TH_NAV_BOT    RGB(  8,  33,  66)   /* 侧栏渐变下端 */
#define TH_NAV_SEL    RGB( 27,  80, 141)   /* 选中项底色 */
#define TH_NAV_HOVER  RGB( 20,  64, 117)
#define TH_TOOLBAR    RGB( 13,  53, 100)
#define TH_TOOLBAR_LN RGB( 30,  74, 126)

#define TH_ON_DARK       RGB(255, 255, 255)   /* 深蓝上的主文字 */
#define TH_ON_DARK_DIM   RGB(176, 197, 224)   /* 深蓝上的次要文字 */
#define TH_ON_DARK_DIM2  RGB(139, 165, 199)

/* ---- 浅色内容区 ---- */
#define TH_BG         RGB(247, 249, 252)
#define TH_CARD       RGB(255, 255, 255)
#define TH_HOVER      RGB(238, 243, 250)
#define TH_TEXT       RGB( 27,  36,  48)
#define TH_TEXT_SUB   RGB(122, 134, 150)
#define TH_TEXT_DIM   RGB(160, 170, 184)
#define TH_LINE       RGB(226, 232, 240)

/* ---- 品牌色 ---- */
#define TH_BLUE       RGB(  6,  75, 150)   /* 校徽主蓝 */
#define TH_BLUE_MID   RGB( 21,  98, 185)
#define TH_BLUE_LIGHT RGB( 42, 124, 210)
#define TH_BLUE_SOFT  RGB(225, 236, 250)
#define TH_BLUE_PALE  RGB(242, 247, 254)
#define TH_WHITE      RGB(255, 255, 255)
#define TH_RED        RGB(226,  74,  74)   /* 收藏心形 */

/* 画校徽风格的五瓣花（旋转椭圆的多边形轮廓，纯 GDI）
   cx,cy 圆心，R 花朵半径，color 花瓣颜色 */
void theme_draw_flower(HDC dc, int cx, int cy, int R, COLORREF color);
/* 实心五瓣花：深蓝圆底 + 白花（设计稿里的标志样子） */
void theme_draw_badge(HDC dc, int cx, int cy, int R, COLORREF blue, COLORREF white);
/* 描边五瓣花：白圈 + 深蓝底 + 白花描边（设计稿侧栏/头像的样子） */
void theme_draw_badge_outline(HDC dc, int cx, int cy, int R, int ringW,
                              COLORREF line, COLORREF fill, COLORREF petal);
/* 竖向渐变填充（设计稿的深蓝背景） */
void theme_fill_vgrad(HDC dc, const RECT *rc, COLORREF top, COLORREF bot);

#endif /* THEME_H */

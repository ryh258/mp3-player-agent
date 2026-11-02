/* cover.h —— 专辑封面：解码与默认封面生成（内部用 GDI+ 实现，对 C 暴露接口） */
#ifndef COVER_H
#define COVER_H

#include <windows.h>

#ifdef __cplusplus
extern "C" {
#endif

/* 启动/关闭 GDI+（进程内调一次即可） */
int  cover_startup(void);
void cover_shutdown(void);

/* 从内存里的图片数据（JPEG/PNG/BMP…）生成边长不超过 maxSize 的方形位图。
   调用方负责 DeleteObject。失败返回 NULL。 */
HBITMAP cover_from_memory(const void *data, int len, int maxSize);

/* 生成一张"校徽风格"的默认封面：渐变底 + 白色五瓣花 + 中间的字符。
   color 传 0 表示按 text 自动取色。调用方负责 DeleteObject。 */
HBITMAP cover_default(int size, const wchar_t *text, COLORREF color);

/* 歌手头像：圆形徽章（白圈 + 蓝底 + 白花）+ 名字首字 */
HBITMAP cover_artist(int size, const wchar_t *name);

/* 把图片等比缩放放进 boxW x boxH 的框里（留黑边，不裁切）。
   启动页是 3:4 竖图，不能用正方形裁剪的那个函数。 */
HBITMAP cover_fit_box(const void *data, int len, int boxW, int boxH);

#ifdef __cplusplus
}
#endif

#endif /* COVER_H */

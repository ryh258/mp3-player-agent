/* wicdec.h —— 用 Windows Imaging Component 解码图片（替代老旧的 OleLoadPicture） */
#ifndef WICDEC_H
#define WICDEC_H

#include <windows.h>

/* 把内存里的图片（JPEG/PNG/GIF/BMP…）渲染成一张 32 位 DIB。
 *
 *   outW x outH  目标尺寸
 *   cropSquare   1 = 按短边等比缩放后居中裁剪成正方形（专辑封面用）
 *                0 = 等比缩放到能放进 outW x outH，不足处填 padColor（启动页用）
 *   padColor     留白填充色
 *
 * 返回的位图 alpha 由 WIC 填成 255，可直接放进 ILC_COLOR32 的 ImageList。
 * 调用方负责 DeleteObject。失败返回 NULL。 */
HBITMAP wic_render(const void *data, int len,
                   int outW, int outH, int cropSquare, COLORREF padColor);

#endif /* WICDEC_H */

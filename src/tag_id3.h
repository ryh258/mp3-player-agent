/* tag_id3.h —— 读取 MP3 的 ID3 标签（v2.2 / v2.3 / v2.4 + v1 兜底） */
#ifndef TAG_ID3_H
#define TAG_ID3_H

#include <windows.h>

#define ID3_TEXT_MAX 256

typedef struct Id3Tags {
    wchar_t title[ID3_TEXT_MAX];
    wchar_t artist[ID3_TEXT_MAX];
    wchar_t album[ID3_TEXT_MAX];
    wchar_t year[16];
    int     track;      /* 0 = 未知；TRCK 形如 "3/12" 时取 3 */
    int     hasV2;      /* 读到了 ID3v2 */
    int     hasV1;      /* 读到了 ID3v1 */
} Id3Tags;

/* 读取标签。tags 会先被清零。
   返回 1 表示至少读到一项内容，0 表示没有标签或读取失败。 */
int id3_read(const wchar_t *path, Id3Tags *tags);

/* 取出内嵌的专辑封面图（ID3v2 的 APIC 帧）。
   成功返回 1 并填 outData（调用方用 free 释放）和 outLen；没有封面返回 0。
   拿到的通常是原始 JPEG/PNG 字节，交给解码器处理即可。 */
int id3_cover(const wchar_t *path, unsigned char **outData, unsigned *outLen);

#endif /* TAG_ID3_H */

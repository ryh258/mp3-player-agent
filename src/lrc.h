/* lrc.h —— LRC 歌词文件解析 */
#ifndef LRC_H
#define LRC_H

#include <windows.h>

typedef struct LrcLine {
    int            timeMs;
    const wchar_t *text;    /* 指向 Lrc.buf 内部，不要单独 free */
} LrcLine;

typedef struct Lrc {
    wchar_t  *buf;          /* 全部歌词文本（连续存放） */
    LrcLine  *lines;
    int       count;
    wchar_t   found[1024];  /* 实际读到的 .lrc 路径；空串 = 没找到 */
    int       offsetMs;     /* [offset:±毫秒] 标签 */
} Lrc;

void lrc_init(Lrc *l);
void lrc_free(Lrc *l);

/* 找同目录同名的 .lrc 并解析。返回歌词行数（0 = 没找到或为空） */
int  lrc_load_for(Lrc *l, const wchar_t *audioPath);

/* 当前时间应高亮哪一行：返回索引；-1 表示还没到第一行 */
int  lrc_index_at(const Lrc *l, int ms);

#endif /* LRC_H */

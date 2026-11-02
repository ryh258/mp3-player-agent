/* tag_id3.c —— 自己解析 MP3 的 ID3 标签，不依赖任何第三方库
 *
 * 支持：
 *   ID3v2.2（3 字符帧 ID）/ v2.3（4 字符，大端长度）/ v2.4（4 字符，同步安全整数长度）
 *   文本编码 0=ISO-8859-1(实际常是 GBK) 1=UTF-16 带 BOM 2=UTF-16BE 3=UTF-8
 *   ID3v1 / ID3v1.1（文件末尾 128 字节，用于补齐 v2 里缺的字段）
 *
 * 只取标题/歌手/专辑/年份/音轨号，其它帧（尤其是动辄上百 KB 的 APIC 封面）按长度跳过。
 *
 * 已知未处理：全局 unsynchronisation（整个标签做了 0xFF00 转义）。
 *   现代编码器基本不用，遇到这种文件就退回到 ID3v1 / 文件名。
 */
#include "tag_id3.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>

#define TAG_READ_MAX (2 * 1024 * 1024)   /* ID3v2 标签读取上限，防止畸形文件吃内存 */

enum { F_TITLE = 0, F_ARTIST, F_ALBUM, F_YEAR, F_TRACK, F_COUNT };

/* 各版本里对应的帧 ID */
static const char *FRAME_V22[F_COUNT] = { "TT2",  "TP1",  "TAL",  "TYE", "TRK" };
static const char *FRAME_V23[F_COUNT] = { "TIT2", "TPE1", "TALB", "TYER", "TRCK" };
static const char *FRAME_V24[F_COUNT] = { "TIT2", "TPE1", "TALB", "TDRC", "TRCK" };

/* ID3v2 的长度是"同步安全整数"：每字节只用低 7 位 */
static DWORD syncsafe(const BYTE *p)
{
    return ((DWORD)(p[0] & 0x7F) << 21) |
           ((DWORD)(p[1] & 0x7F) << 14) |
           ((DWORD)(p[2] & 0x7F) << 7)  |
           ((DWORD)(p[3] & 0x7F));
}

/* 是不是合法的 UTF-8 字节序列（用于判断 enc=0 的标签到底用的什么编码） */
static int is_utf8(const BYTE *s, int n)
{
    int i = 0, hasHigh = 0;
    while (i < n) {
        BYTE c = s[i];
        int extra;
        if (c < 0x80) { i++; continue; }
        hasHigh = 1;
        if ((c & 0xE0) == 0xC0) extra = 1;
        else if ((c & 0xF0) == 0xE0) extra = 2;
        else if ((c & 0xF8) == 0xF0) extra = 3;
        else return 0;
        if (i + extra >= n) return 0;
        while (extra--) {
            i++;
            if ((s[i] & 0xC0) != 0x80) return 0;
        }
        i++;
    }
    return hasHigh ? 1 : -1;   /* -1 表示纯 ASCII */
}

/* 解一个文本帧：p[0] 是编码字节，后面是内容 */
static void decode_text(const BYTE *p, int len, wchar_t *out, size_t cch)
{
    BYTE enc;
    const BYTE *s;
    int n, i, j;

    if (!out || cch == 0)
        return;
    out[0] = 0;
    if (len <= 1)
        return;

    enc = p[0];
    s = p + 1;
    n = len - 1;

    /* 去掉尾部填充的 0 */
    while (n > 0 && s[n - 1] == 0)
        n--;
    if (n <= 0)
        return;

    /* --- UTF-16 --- */
    if (enc == 1 || enc == 2) {
        int le = (enc != 2);
        if (enc == 1 && n >= 2) {
            if (s[0] == 0xFF && s[1] == 0xFE)      { le = 1; s += 2; n -= 2; }
            else if (s[0] == 0xFE && s[1] == 0xFF) { le = 0; s += 2; n -= 2; }
        }
        j = 0;
        for (i = 0; i + 1 < n && j < (int)cch - 1; i += 2) {
            wchar_t wc = le ? (wchar_t)(s[i] | (s[i + 1] << 8))
                            : (wchar_t)((s[i] << 8) | s[i + 1]);
            if (wc == 0)
                break;
            out[j++] = wc;
        }
        out[j] = 0;
        return;
    }

    /* --- 单字节编码 --- */
    {
        UINT cp;
        int w;

        if (enc == 3) {
            cp = CP_UTF8;
        } else {
            /* 规范说 enc=0 是 ISO-8859-1，但中文 MP3 几乎都写成了 GBK。
               先看是不是合法 UTF-8，是就按 UTF-8；否则按系统 ANSI（中文是 936/GBK）。
               纯 ASCII 两种解出来一样，不用纠结。 */
            int u = is_utf8(s, n);
            cp = (u == 1) ? CP_UTF8 : CP_ACP;
        }

        w = MultiByteToWideChar(cp, 0, (LPCSTR)s, n, out, (int)cch - 1);
        if (w < 0)
            w = 0;
        out[w] = 0;
    }
}

/* ID3v1 的字段：定长、补空格或 0，按系统 ANSI 解码 */
static void ansi_field(const BYTE *p, int n, wchar_t *out, size_t cch)
{
    int w;
    if (!out || cch == 0)
        return;
    out[0] = 0;
    while (n > 0 && (p[n - 1] == 0 || p[n - 1] == ' '))
        n--;
    if (n <= 0)
        return;
    w = MultiByteToWideChar(CP_ACP, 0, (LPCSTR)p, n, out, (int)cch - 1);
    if (w < 0)
        w = 0;
    out[w] = 0;
}

/* --- ID3v2 --- */
static int parse_v2(const BYTE *buf, DWORD size, int major, int flags, Id3Tags *t)
{
    DWORD pos = 0;
    int got = 0;
    int hdr = (major == 2) ? 6 : 10;

    if (flags & 0x80)
        return 0;   /* 全局 unsynchronisation，不处理 */

    /* 扩展头：跳过去 */
    if (flags & 0x40) {
        if (major == 3 && size >= 4)
            pos = 4 + (((DWORD)buf[0] << 24) | ((DWORD)buf[1] << 16) |
                       ((DWORD)buf[2] << 8) | buf[3]);
        else if (major == 4 && size >= 4)
            pos = syncsafe(buf);           /* v2.4 的长度含自身 */
        if (pos >= size)
            return 0;
    }

    while (pos + (DWORD)hdr <= size) {
        const BYTE *fr = buf + pos;
        char id[8];
        DWORD fsz;
        int i;

        if (fr[0] == 0)
            break;                          /* 到填充区 */

        if (major == 2) {
            memcpy(id, fr, 3);
            id[3] = 0;
            fsz = ((DWORD)fr[3] << 16) | ((DWORD)fr[4] << 8) | fr[5];
        } else {
            memcpy(id, fr, 4);
            id[4] = 0;
            if (major == 4)
                fsz = syncsafe(fr + 4);
            else
                fsz = ((DWORD)fr[4] << 24) | ((DWORD)fr[5] << 16) |
                      ((DWORD)fr[6] << 8) | fr[7];
        }

        if (fsz == 0 || pos + (DWORD)hdr + fsz > size)
            break;                          /* 长度不合理，收工 */

        for (i = 0; i < F_COUNT; i++) {
            const char *want = (major == 2) ? FRAME_V22[i] :
                               (major == 4) ? FRAME_V24[i] : FRAME_V23[i];
            if (strcmp(id, want) != 0)
                continue;

            if (i == F_TRACK) {
                wchar_t tmp[32];
                decode_text(fr + hdr, (int)fsz, tmp, 32);
                t->track = _wtoi(tmp);      /* "3/12" 会取到 3 */
            } else {
                wchar_t *dst = (i == F_TITLE)  ? t->title  :
                               (i == F_ARTIST) ? t->artist :
                               (i == F_ALBUM)  ? t->album  : t->year;
                size_t cch = (i == F_YEAR) ? 16 : ID3_TEXT_MAX;
                decode_text(fr + hdr, (int)fsz, dst, cch);
                if (dst[0])
                    got++;
            }
            break;
        }

        pos += (DWORD)hdr + fsz;
    }

    if (got > 0)
        t->hasV2 = 1;
    return got;
}

/* --- ID3v1：只补 v2 里缺的字段 --- */
static int parse_v1(const BYTE *v, Id3Tags *t)
{
    int got = 0;

    if (!t->title[0])  { ansi_field(v + 3,  30, t->title,  ID3_TEXT_MAX); if (t->title[0])  got++; }
    if (!t->artist[0]) { ansi_field(v + 33, 30, t->artist, ID3_TEXT_MAX); if (t->artist[0]) got++; }
    if (!t->album[0])  { ansi_field(v + 63, 30, t->album,  ID3_TEXT_MAX); if (t->album[0])  got++; }
    if (!t->year[0])   { ansi_field(v + 93, 4,  t->year,   16);           if (t->year[0])   got++; }
    if (t->track == 0 && v[125] == 0 && v[126] != 0)
        t->track = v[126];                  /* ID3v1.1 的音轨号藏在这里 */

    if (got > 0)
        t->hasV1 = 1;
    return got;
}

int id3_read(const wchar_t *path, Id3Tags *tags)
{
    FILE *f;
    BYTE head[10];
    int got = 0;

    if (!tags)
        return 0;
    memset(tags, 0, sizeof(*tags));
    if (!path)
        return 0;

    f = _wfopen(path, L"rb");
    if (!f)
        return 0;

    /* --- ID3v2（文件开头） --- */
    if (fread(head, 1, 10, f) == 10 && memcmp(head, "ID3", 3) == 0) {
        int major = head[3];
        int flags = head[5];
        DWORD size = syncsafe(head + 6);

        if (major >= 2 && major <= 4 && size > 0 && size < TAG_READ_MAX) {
            BYTE *buf = (BYTE *)malloc(size);
            if (buf) {
                if (fread(buf, 1, size, f) == size)
                    got += parse_v2(buf, size, major, flags, tags);
                free(buf);
            }
        }
    }

    /* --- ID3v1（文件末尾 128 字节） --- */
    if (fseek(f, -128, SEEK_END) == 0) {
        BYTE v1[128];
        if (fread(v1, 1, 128, f) == 128 && memcmp(v1, "TAG", 3) == 0)
            got += parse_v1(v1, tags);
    }

    fclose(f);
    return got > 0;
}

/* ---- 内嵌封面：从 APIC(v2.3/2.4) / PIC(v2.2) 帧里取出图片数据 ----
   APIC 布局：编码(1) + MIME(0结尾) + 图片类型(1) + 描述(按编码0结尾) + 图片数据
   PIC  布局：编码(1) + 图片格式(3) + 图片类型(1) + 描述 + 图片数据            */
static int extract_picture(const BYTE *d, DWORD n, int major,
                           unsigned char **out, unsigned *outLen)
{
    BYTE enc;
    DWORD p = 1;

    if (n < 6)
        return 0;
    enc = d[0];

    if (major == 2) {
        p += 3;                       /* 图片格式 3 字节 */
    } else {
        while (p < n && d[p] != 0) p++;   /* MIME */
        p++;
    }
    if (p >= n) return 0;
    p += 1;                            /* 图片类型 */

    if (enc == 1 || enc == 2) {        /* UTF-16 描述 */
        while (p + 1 < n && !(d[p] == 0 && d[p + 1] == 0)) p += 2;
        p += 2;
    } else {
        while (p < n && d[p] != 0) p++;
        p++;
    }
    if (p >= n) return 0;

    *outLen = n - p;
    *out = (unsigned char *)malloc(*outLen);
    if (!*out) { *outLen = 0; return 0; }
    memcpy(*out, d + p, *outLen);
    return 1;
}

int id3_cover(const wchar_t *path, unsigned char **outData, unsigned *outLen)
{
    FILE *f;
    BYTE head[10];
    BYTE *buf = NULL;
    DWORD size;
    int major, ok = 0;
    DWORD pos;
    int hdr;

    if (outData) *outData = NULL;
    if (outLen) *outLen = 0;
    if (!path || !outData || !outLen)
        return 0;

    f = _wfopen(path, L"rb");
    if (!f)
        return 0;

    if (fread(head, 1, 10, f) != 10 || memcmp(head, "ID3", 3) != 0) {
        fclose(f);
        return 0;
    }
    major = head[3];
    size = syncsafe(head + 6);
    if (major < 2 || major > 4 || size == 0 || size > TAG_READ_MAX) {
        fclose(f);
        return 0;
    }

    buf = (BYTE *)malloc(size);
    if (!buf) { fclose(f); return 0; }
    if (fread(buf, 1, size, f) != size) {
        free(buf);
        fclose(f);
        return 0;
    }
    fclose(f);

    hdr = (major == 2) ? 6 : 10;
    pos = 0;
    while (pos + (DWORD)hdr <= size) {
        const BYTE *fr = buf + pos;
        char id[8];
        DWORD fsz;

        if (fr[0] == 0)
            break;                     /* 到填充区 */

        if (major == 2) {
            memcpy(id, fr, 3); id[3] = 0;
            fsz = ((DWORD)fr[3] << 16) | ((DWORD)fr[4] << 8) | fr[5];
        } else {
            memcpy(id, fr, 4); id[4] = 0;
            fsz = (major == 4) ? syncsafe(fr + 4)
                               : (((DWORD)fr[4] << 24) | ((DWORD)fr[5] << 16) |
                                  ((DWORD)fr[6] << 8) | fr[7]);
        }
        if (fsz == 0 || pos + (DWORD)hdr + fsz > size)
            break;

        if ((major == 2 && strcmp(id, "PIC") == 0) ||
            (major != 2 && strcmp(id, "APIC") == 0)) {
            if (extract_picture(fr + hdr, fsz, major, outData, outLen)) {
                ok = 1;
                break;
            }
        }
        pos += (DWORD)hdr + fsz;
    }

    free(buf);
    return ok;
}

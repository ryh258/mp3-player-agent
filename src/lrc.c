/* lrc.c —— LRC 歌词文件解析
 *
 * 支持的时间标签形式：
 *   [mm:ss]  [mm:ss.xx]  [mm:ss.xxx]  [mm:ss:xx]（有些老编辑器的写法）
 * 一行可以有多个时间标签（同一句歌词重复出现）：
 *   [00:12.00][01:20.50]这是一句歌词
 * 元信息标签（[ti:] [ar:] [al:] [by:] 等）会被跳过，
 * 但 [offset:±毫秒] 会真正生效（整体提前/延后）。
 *
 * 编码：自动识别 UTF-8 BOM / UTF-16 BOM；没有 BOM 时先试 UTF-8，
 *       不是合法 UTF-8 就按系统 ANSI（中文是 GBK）解 —— 中文歌词文件
 *       这两种情况都很常见。
 */
#include "lrc.h"
#include "util.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>

void lrc_init(Lrc *l)
{
    memset(l, 0, sizeof(*l));
}

void lrc_free(Lrc *l)
{
    if (!l)
        return;
    free(l->buf);
    free(l->lines);
    memset(l, 0, sizeof(*l));
}

/* 是不是合法 UTF-8（返回 -1 表示纯 ASCII） */
static int is_utf8(const unsigned char *s, int n)
{
    int i = 0, high = 0;
    while (i < n) {
        unsigned char c = s[i];
        int extra;
        if (c < 0x80) { i++; continue; }
        high = 1;
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
    return high ? 1 : -1;
}

/* 把文件内容转成宽字符串（新分配） */
static wchar_t *to_wide(const unsigned char *data, int len, int *outChars)
{
    wchar_t *out;

    if (outChars) *outChars = 0;
    if (len <= 0)
        return NULL;

    /* UTF-16 LE BOM */
    if (len >= 2 && data[0] == 0xFF && data[1] == 0xFE) {
        int n = (len - 2) / 2, i;
        out = (wchar_t *)malloc((size_t)(n + 1) * sizeof(wchar_t));
        if (!out) return NULL;
        for (i = 0; i < n; i++)
            out[i] = (wchar_t)(data[2 + i * 2] | (data[3 + i * 2] << 8));
        out[n] = 0;
        if (outChars) *outChars = n;
        return out;
    }
    /* UTF-16 BE BOM */
    if (len >= 2 && data[0] == 0xFE && data[1] == 0xFF) {
        int n = (len - 2) / 2, i;
        out = (wchar_t *)malloc((size_t)(n + 1) * sizeof(wchar_t));
        if (!out) return NULL;
        for (i = 0; i < n; i++)
            out[i] = (wchar_t)((data[2 + i * 2] << 8) | data[3 + i * 2]);
        out[n] = 0;
        if (outChars) *outChars = n;
        return out;
    }

    /* UTF-8 BOM */
    if (len >= 3 && data[0] == 0xEF && data[1] == 0xBB && data[2] == 0xBF) {
        data += 3;
        len -= 3;
    }

    {
        UINT cp = CP_UTF8;
        int u = is_utf8(data, len);
        if (u == 0)
            cp = CP_ACP;                 /* 不是 UTF-8，按 GBK 解 */
        {
            int w = MultiByteToWideChar(cp, 0, (LPCSTR)data, len, NULL, 0);
            if (w <= 0)
                return NULL;
            out = (wchar_t *)malloc((size_t)(w + 1) * sizeof(wchar_t));
            if (!out)
                return NULL;
            w = MultiByteToWideChar(cp, 0, (LPCSTR)data, len, out, w);
            if (w < 0) w = 0;
            out[w] = 0;
            if (outChars) *outChars = w;
            return out;
        }
    }
}

/* 解析 [mm:ss.xx] 这类时间标签；成功返回消耗的字符数，失败返回 0 */
static int parse_time_tag(const wchar_t *s, int *outMs)
{
    const wchar_t *p = s;
    int mm, ss, frac = 0, fracDigits = 0;

    if (*p != L'[') return 0;
    p++;

    if (!iswdigit(*p)) return 0;
    mm = 0;
    while (iswdigit(*p)) { mm = mm * 10 + (*p - L'0'); p++; }

    if (*p != L':') return 0;
    p++;

    if (!iswdigit(*p)) return 0;
    ss = 0;
    while (iswdigit(*p)) { ss = ss * 10 + (*p - L'0'); p++; }

    if (*p == L'.' || *p == L':') {
        p++;
        while (iswdigit(*p)) {
            if (fracDigits < 3) {
                frac = frac * 10 + (*p - L'0');
                fracDigits++;
            }
            p++;
        }
    }

    if (*p != L']') return 0;
    p++;

    /* 毫秒：.xx = 百分秒，.xxx = 毫秒 */
    while (fracDigits < 3) { frac *= 10; fracDigits++; }

    *outMs = (mm * 60 + ss) * 1000 + frac;
    return (int)(p - s);
}

static int cmp_line(const void *a, const void *b)
{
    const LrcLine *x = (const LrcLine *)a, *y = (const LrcLine *)b;
    return (x->timeMs > y->timeMs) - (x->timeMs < y->timeMs);
}

int lrc_load_for(Lrc *l, const wchar_t *audioPath)
{
    wchar_t lrcPath[1024];
    FILE *f;
    long sz;
    unsigned char *raw;
    wchar_t *text = NULL;
    int chars = 0;
    const wchar_t *p;
    int cap = 0;
    wchar_t *textStore = NULL;
    int textUsed = 0;

    lrc_init(l);
    if (!audioPath)
        return 0;

    wcsncpy(lrcPath, audioPath, 1020);
    lrcPath[1020] = 0;
    {
        wchar_t *dot = wcsrchr(lrcPath, L'.');
        wchar_t *slash = wcsrchr(lrcPath, L'\\');
        if (dot && (!slash || dot > slash))
            wcscpy(dot, L".lrc");
        else
            wcscat(lrcPath, L".lrc");
    }

    f = _wfopen(lrcPath, L"rb");
    if (!f)
        return 0;

    fseek(f, 0, SEEK_END);
    sz = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (sz <= 0 || sz > 4 * 1024 * 1024) {
        fclose(f);
        return 0;
    }

    raw = (unsigned char *)malloc((size_t)sz);
    if (!raw) {
        fclose(f);
        return 0;
    }
    if (fread(raw, 1, (size_t)sz, f) != (size_t)sz) {
        free(raw);
        fclose(f);
        return 0;
    }
    fclose(f);

    text = to_wide(raw, (int)sz, &chars);
    free(raw);
    if (!text)
        return 0;

    wcsncpy(l->found, lrcPath, 1023);

    /* 先扫一遍 offset 标签 */
    {
        const wchar_t *q = text;
        while ((q = wcsstr(q, L"[offset:")) != NULL) {
            l->offsetMs = _wtoi(q + 8);
            break;
        }
    }

    /* 估算行数上限 */
    cap = chars / 4 + 16;
    l->lines = (LrcLine *)malloc((size_t)cap * sizeof(LrcLine));
    textStore = (wchar_t *)malloc((size_t)(chars + 1) * sizeof(wchar_t));
    if (!l->lines || !textStore) {
        free(text);
        free(l->lines);
        free(textStore);
        lrc_init(l);
        return 0;
    }
    l->buf = textStore;

    p = text;
    while (*p) {
        const wchar_t *lineEnd = wcschr(p, L'\n');
        int lineLen = lineEnd ? (int)(lineEnd - p) : (int)wcslen(p);
        const wchar_t *q = p;
        int times[16], nTimes = 0;
        int consumed;

        /* 收集行首连续的时间标签 */
        while (nTimes < 16 && (consumed = parse_time_tag(q, &times[nTimes])) > 0) {
            q += consumed;
            nTimes++;
        }

        if (nTimes > 0) {
            /* 剩下的部分是歌词文本 */
            int tlen = lineLen - (int)(q - p);
            const wchar_t *txt = q;
            int i;

            while (tlen > 0 && (txt[tlen - 1] == L'\r' || txt[tlen - 1] == L' '))
                tlen--;

            if (l->count + nTimes > cap) {
                int nc = cap * 2 + nTimes;
                LrcLine *nl = (LrcLine *)realloc(l->lines, (size_t)nc * sizeof(LrcLine));
                if (!nl) break;
                l->lines = nl;
                cap = nc;
            }

            for (i = 0; i < nTimes; i++) {
                wchar_t *dst = textStore + textUsed;
                memcpy(dst, txt, (size_t)tlen * sizeof(wchar_t));
                dst[tlen] = 0;
                textUsed += tlen + 1;

                l->lines[l->count].timeMs = times[i] + l->offsetMs;
                l->lines[l->count].text = dst;
                l->count++;
            }
        }

        if (!lineEnd)
            break;
        p = lineEnd + 1;
    }

    free(text);

    if (l->count > 1)
        qsort(l->lines, (size_t)l->count, sizeof(LrcLine), cmp_line);

    return l->count;
}

int lrc_index_at(const Lrc *l, int ms)
{
    int lo = 0, hi, best = -1;

    if (!l || l->count <= 0)
        return -1;

    hi = l->count - 1;
    while (lo <= hi) {
        int mid = (lo + hi) / 2;
        if (l->lines[mid].timeMs <= ms) {
            best = mid;
            lo = mid + 1;
        } else {
            hi = mid - 1;
        }
    }
    return best;
}

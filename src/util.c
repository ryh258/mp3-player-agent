/* util.c */
#include "util.h"
#include <wchar.h>
#include <string.h>

void util_format_time(int ms, wchar_t *buf, size_t cch)
{
    int total, h, m, s;

    if (!buf || cch == 0)
        return;

    if (ms < 0) {
        wcsncpy(buf, L"--:--", cch);
        buf[cch - 1] = 0;
        return;
    }

    total = ms / 1000;
    h = total / 3600;
    m = (total / 60) % 60;
    s = total % 60;

    if (h > 0)
        swprintf(buf, cch, L"%d:%02d:%02d", h, m, s);
    else
        swprintf(buf, cch, L"%02d:%02d", m, s);
}

const wchar_t *util_basename(const wchar_t *path)
{
    const wchar_t *p, *last;

    if (!path)
        return L"";

    last = path;
    for (p = path; *p; p++) {
        if (*p == L'\\' || *p == L'/')
            last = p + 1;
    }
    return last;
}

const wchar_t *util_ext(const wchar_t *path)
{
    const wchar_t *base, *p, *dot = NULL;

    if (!path)
        return L"";

    base = util_basename(path);
    for (p = base; *p; p++) {
        if (*p == L'.')
            dot = p;
    }
    return dot ? dot : L"";
}

int util_is_audio_file(const wchar_t *path)
{
    static const wchar_t *exts[] = {
        L".mp3", L".wav", L".m4a", L".wma", L".aac",
        L".mp2", L".mpa", L".aif", L".aiff", L".au", L".snd"
    };
    const wchar_t *e = util_ext(path);
    size_t i;

    if (*e == 0)
        return 0;

    for (i = 0; i < sizeof(exts) / sizeof(exts[0]); i++) {
        if (_wcsicmp(e, exts[i]) == 0)
            return 1;
    }
    return 0;
}

void util_title_from_path(const wchar_t *path, wchar_t *out, size_t cch)
{
    const wchar_t *base;
    size_t n;

    if (!out || cch == 0)
        return;

    base = util_basename(path);
    wcsncpy(out, base, cch);
    out[cch - 1] = 0;

    /* 去掉扩展名：找到标题里最后一个点（不是开头那个） */
    n = wcslen(out);
    while (n > 1) {
        n--;
        if (out[n] == L'.') {
            out[n] = 0;
            break;
        }
    }
}

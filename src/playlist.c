/* playlist.c */
#include "playlist.h"
#include "util.h"
#include "tag_id3.h"
#include "media_len.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>

#define SCAN_MAX_DEPTH 16

void playlist_init(Playlist *pl)
{
    pl->items = NULL;
    pl->count = 0;
    pl->cap = 0;
}

void playlist_free(Playlist *pl)
{
    free(pl->items);
    playlist_init(pl);
}

void playlist_clear(Playlist *pl)
{
    pl->count = 0;
}

static int ensure_cap(Playlist *pl, int need)
{
    Track *p;
    int cap;

    if (need <= pl->cap)
        return 1;

    cap = pl->cap ? pl->cap : 64;
    while (cap < need)
        cap *= 2;

    p = (Track *)realloc(pl->items, (size_t)cap * sizeof(Track));
    if (!p)
        return 0;

    pl->items = p;
    pl->cap = cap;
    return 1;
}

int playlist_find(const Playlist *pl, const wchar_t *path)
{
    int i;
    if (!pl || !path)
        return -1;
    for (i = 0; i < pl->count; i++) {
        if (_wcsicmp(pl->items[i].path, path) == 0)
            return i;
    }
    return -1;
}

int playlist_add(Playlist *pl, const wchar_t *path)
{
    Track *t;
    Id3Tags tags;

    if (!pl || !path || !*path)
        return -1;
    if (playlist_find(pl, path) >= 0)
        return -1;
    if (!ensure_cap(pl, pl->count + 1))
        return -1;

    t = &pl->items[pl->count];
    memset(t, 0, sizeof(*t));
    wcsncpy(t->path, path, TRACK_PATH_MAX);
    t->path[TRACK_PATH_MAX - 1] = 0;
    util_title_from_path(path, t->title, TRACK_TITLE_MAX);

    /* 有 ID3 标签就显示真实歌名/歌手，没有就保持文件名 */
    if (id3_read(path, &tags)) {
        if (tags.title[0]) {
            wcsncpy(t->title, tags.title, TRACK_TITLE_MAX);
            t->title[TRACK_TITLE_MAX - 1] = 0;
        }
        if (tags.artist[0]) {
            wcsncpy(t->artist, tags.artist, TRACK_META_MAX);
            t->artist[TRACK_META_MAX - 1] = 0;
        }
        if (tags.album[0]) {
            wcsncpy(t->album, tags.album, TRACK_META_MAX);
            t->album[TRACK_META_MAX - 1] = 0;
        }
    }

    /* 时长：先自己解析出来，这样还没播过的曲目也能显示时长。
       真正播放时会用解码器给的精确值覆盖它。 */
    t->length_ms = media_length_ms(path);

    pl->count++;
    return pl->count - 1;
}

int playlist_remove(Playlist *pl, int idx)
{
    if (!pl || idx < 0 || idx >= pl->count)
        return 0;

    if (idx < pl->count - 1) {
        memmove(&pl->items[idx], &pl->items[idx + 1],
                (size_t)(pl->count - idx - 1) * sizeof(Track));
    }
    pl->count--;
    return 1;
}

int playlist_append_track(Playlist *pl, const Track *t)
{
    Track *d;

    if (!pl || !t || !t->path[0])
        return -1;
    if (playlist_find(pl, t->path) >= 0)
        return -1;
    if (!ensure_cap(pl, pl->count + 1))
        return -1;

    d = &pl->items[pl->count];
    *d = *t;                       /* 整条复制，ID3 信息一起带过来 */
    pl->count++;
    return pl->count - 1;
}

/* 拼接目录与文件名，自动处理结尾反斜杠 */
static int join_path(const wchar_t *dir, const wchar_t *name,
                     wchar_t *out, size_t cch)
{
    size_t n = wcslen(dir);
    int needSep = (n > 0 && dir[n - 1] != L'\\' && dir[n - 1] != L'/');

    if (wcslen(dir) + wcslen(name) + 2 > cch)
        return 0;

    if (needSep)
        swprintf(out, cch, L"%ls\\%ls", dir, name);
    else
        swprintf(out, cch, L"%ls%ls", dir, name);
    return 1;
}

static int scan_rec(Playlist *pl, const wchar_t *dir, int recursive, int depth)
{
    wchar_t pattern[TRACK_PATH_MAX];
    WIN32_FIND_DATAW fd;
    HANDLE h;
    int added = 0;

    if (depth > SCAN_MAX_DEPTH)
        return 0;
    if (wcslen(dir) + 4 >= TRACK_PATH_MAX)
        return 0;

    if (!join_path(dir, L"*", pattern, TRACK_PATH_MAX))
        return 0;

    h = FindFirstFileW(pattern, &fd);
    if (h == INVALID_HANDLE_VALUE)
        return 0;

    do {
        wchar_t full[TRACK_PATH_MAX];

        if (wcscmp(fd.cFileName, L".") == 0 ||
            wcscmp(fd.cFileName, L"..") == 0)
            continue;

        if (!join_path(dir, fd.cFileName, full, TRACK_PATH_MAX))
            continue;

        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
            if (recursive)
                added += scan_rec(pl, full, recursive, depth + 1);
        } else if (util_is_audio_file(fd.cFileName)) {
            if (playlist_add(pl, full) >= 0)
                added++;
        }
    } while (FindNextFileW(h, &fd));

    FindClose(h);
    return added;
}

int library_scan_dir(Playlist *pl, const wchar_t *dir, int recursive)
{
    if (!pl || !dir || !*dir)
        return 0;
    return scan_rec(pl, dir, recursive, 0);
}

int playlist_copy_paths(Playlist *dst, const Playlist *src)
{
    int i, n = 0;
    if (!dst || !src)
        return 0;
    for (i = 0; i < src->count; i++) {
        if (playlist_add(dst, src->items[i].path) >= 0)
            n++;
    }
    return n;
}

int playlist_save_m3u(const wchar_t *file, const Playlist *pl)
{
    FILE *f;
    int i;

    if (!file || !pl)
        return 0;
    f = _wfopen(file, L"w, ccs=UTF-8");
    if (!f)
        return 0;

    fwprintf(f, L"#EXTM3U\n");
    for (i = 0; i < pl->count; i++) {
        int sec = pl->items[i].length_ms > 0 ? pl->items[i].length_ms / 1000 : -1;
        fwprintf(f, L"#EXTINF:%d,%ls - %ls\n", sec,
                 pl->items[i].artist[0] ? pl->items[i].artist : L"未知",
                 pl->items[i].title);
        fwprintf(f, L"%ls\n", pl->items[i].path);
    }
    fclose(f);
    return 1;
}

int playlist_load_m3u(const wchar_t *file, Playlist *pl)
{
    FILE *f;
    int added = 0;

    if (!file || !pl)
        return 0;
    f = _wfopen(file, L"r, ccs=UTF-8");
    if (!f)
        return 0;

    for (;;) {
        wchar_t line[2048];
        size_t n;

        if (!fgetws(line, 2048, f))
            break;
        n = wcslen(line);
        while (n > 0 && (line[n - 1] == L'\n' || line[n - 1] == L'\r')) {
            line[n - 1] = 0;
            n--;
        }
        if (n == 0 || line[0] == L'#')
            continue;
        if (playlist_add(pl, line) >= 0)
            added++;
    }
    fclose(f);
    return added;
}

/* store.c —— 应用数据持久化
 *
 * 文本文件一律用 UTF-8（_wfopen 的 "ccs=UTF-8" 模式），
 * 这样中文歌词名、中文路径都不会乱码。
 */
#include "store.h"
#include "util.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>
#include <time.h>

#define CFG_MAX_KEYS 64
#define LINE_MAX     4096

static wchar_t g_dir[TRACK_PATH_MAX];

/* ---------------- 配置 ---------------- */
typedef struct CfgItem {
    wchar_t key[64];
    wchar_t val[TRACK_PATH_MAX];
} CfgItem;

static CfgItem g_cfg[CFG_MAX_KEYS];
static int     g_cfgCount;
static int     g_cfgDirty;
static wchar_t g_cfgRet[TRACK_PATH_MAX];   /* store_get_str 的返回缓冲 */

/* ---------------- 小工具 ---------------- */

static void join(wchar_t *out, size_t cch, const wchar_t *a, const wchar_t *b)
{
    size_t n = wcslen(a);
    if (n > 0 && (a[n - 1] == L'\\' || a[n - 1] == L'/'))
        swprintf(out, cch, L"%ls%ls", a, b);
    else
        swprintf(out, cch, L"%ls\\%ls", a, b);
}

/* 去掉行尾的 CR/LF */
static void chomp(wchar_t *s)
{
    size_t n = wcslen(s);
    while (n > 0 && (s[n - 1] == L'\n' || s[n - 1] == L'\r')) {
        s[n - 1] = 0;
        n--;
    }
}

int store_init(void)
{
    wchar_t exe[TRACK_PATH_MAX];
    wchar_t *p;

    GetModuleFileNameW(NULL, exe, TRACK_PATH_MAX);
    p = wcsrchr(exe, L'\\');
    if (p) *p = 0;                       /* 去掉文件名，留目录 */

    join(g_dir, TRACK_PATH_MAX, exe, L"data");
    CreateDirectoryW(g_dir, NULL);       /* 已存在会失败，无所谓 */

    {
        wchar_t pl[TRACK_PATH_MAX];
        join(pl, TRACK_PATH_MAX, g_dir, L"playlists");
        CreateDirectoryW(pl, NULL);
    }

    return 1;
}

const wchar_t *store_dir(void)
{
    return g_dir;
}

/* ---------------- 配置读写 ---------------- */

static CfgItem *cfg_find(const wchar_t *key)
{
    int i;
    for (i = 0; i < g_cfgCount; i++) {
        if (_wcsicmp(g_cfg[i].key, key) == 0)
            return &g_cfg[i];
    }
    return NULL;
}

void store_load_config(void)
{
    wchar_t path[TRACK_PATH_MAX];
    FILE *f;

    g_cfgCount = 0;
    g_cfgDirty = 0;
    join(path, TRACK_PATH_MAX, g_dir, L"config.ini");

    f = _wfopen(path, L"r, ccs=UTF-8");
    if (!f)
        return;

    for (;;) {
        wchar_t line[LINE_MAX];
        wchar_t *eq;

        if (!fgetws(line, LINE_MAX, f))
            break;
        chomp(line);
        if (line[0] == 0 || line[0] == L'#' || line[0] == L';')
            continue;
        eq = wcschr(line, L'=');
        if (!eq)
            continue;
        *eq = 0;

        if (g_cfgCount < CFG_MAX_KEYS) {
            wcsncpy(g_cfg[g_cfgCount].key, line, 63);
            g_cfg[g_cfgCount].key[63] = 0;
            wcsncpy(g_cfg[g_cfgCount].val, eq + 1, TRACK_PATH_MAX - 1);
            g_cfg[g_cfgCount].val[TRACK_PATH_MAX - 1] = 0;
            g_cfgCount++;
        }
    }
    fclose(f);
}

void store_save_config(void)
{
    wchar_t path[TRACK_PATH_MAX];
    FILE *f;
    int i;

    join(path, TRACK_PATH_MAX, g_dir, L"config.ini");
    f = _wfopen(path, L"w, ccs=UTF-8");
    if (!f)
        return;

    fwprintf(f, L"# MP3 播放器配置文件（程序自动维护）\n");
    for (i = 0; i < g_cfgCount; i++)
        fwprintf(f, L"%ls=%ls\n", g_cfg[i].key, g_cfg[i].val);
    fclose(f);
    g_cfgDirty = 0;
}

int store_get_int(const wchar_t *key, int def)
{
    CfgItem *it = cfg_find(key);
    if (!it || !it->val[0])
        return def;
    return _wtoi(it->val);
}

void store_set_int(const wchar_t *key, int val)
{
    wchar_t buf[64];
    swprintf(buf, 64, L"%d", val);
    store_set_str(key, buf);
}

const wchar_t *store_get_str(const wchar_t *key)
{
    CfgItem *it = cfg_find(key);
    g_cfgRet[0] = 0;
    if (it) {
        wcsncpy(g_cfgRet, it->val, TRACK_PATH_MAX - 1);
        g_cfgRet[TRACK_PATH_MAX - 1] = 0;
    }
    return g_cfgRet;
}

void store_set_str(const wchar_t *key, const wchar_t *val)
{
    CfgItem *it = cfg_find(key);

    if (!it) {
        if (g_cfgCount >= CFG_MAX_KEYS)
            return;
        it = &g_cfg[g_cfgCount++];
        wcsncpy(it->key, key, 63);
        it->key[63] = 0;
        it->val[0] = 0;
    }
    if (_wcsicmp(it->val, val) == 0)
        return;                          /* 没变就不标脏 */
    wcsncpy(it->val, val, TRACK_PATH_MAX - 1);
    it->val[TRACK_PATH_MAX - 1] = 0;
    g_cfgDirty = 1;
}

/* ---------------- 我的歌曲 ---------------- */

int store_load_library(Playlist *pl)
{
    wchar_t path[TRACK_PATH_MAX];
    FILE *f;

    playlist_clear(pl);
    join(path, TRACK_PATH_MAX, g_dir, L"library.txt");
    f = _wfopen(path, L"r, ccs=UTF-8");
    if (!f)
        return 0;

    for (;;) {
        wchar_t line[LINE_MAX];
        if (!fgetws(line, LINE_MAX, f))
            break;
        chomp(line);
        if (line[0] == 0)
            continue;
        if (GetFileAttributesW(line) == INVALID_FILE_ATTRIBUTES)
            continue;                    /* 文件不在了就跳过 */
        playlist_add(pl, line);
    }
    fclose(f);
    return pl->count;
}

int store_save_library(const Playlist *pl)
{
    wchar_t path[TRACK_PATH_MAX];
    FILE *f;
    int i;

    join(path, TRACK_PATH_MAX, g_dir, L"library.txt");
    f = _wfopen(path, L"w, ccs=UTF-8");
    if (!f)
        return 0;
    for (i = 0; i < pl->count; i++)
        fwprintf(f, L"%ls\n", pl->items[i].path);
    fclose(f);
    return 1;
}

/* ---------------- 最近播放 ---------------- */

int store_load_recent(Playlist *pl, RecentEntry **entries)
{
    wchar_t path[TRACK_PATH_MAX];
    FILE *f;
    RecentEntry *arr = NULL;
    int n = 0, cap = 0;

    playlist_clear(pl);
    if (entries)
        *entries = NULL;

    join(path, TRACK_PATH_MAX, g_dir, L"recent.txt");
    f = _wfopen(path, L"r, ccs=UTF-8");
    if (!f)
        return 0;

    for (;;) {
        wchar_t line[LINE_MAX];
        wchar_t *p1, *p2;

        if (!fgetws(line, LINE_MAX, f))
            break;
        chomp(line);
        if (line[0] == 0 || line[0] == L'#')
            continue;

        p1 = wcschr(line, L'|');
        if (!p1)
            continue;
        *p1 = 0;
        p2 = wcschr(p1 + 1, L'|');
        if (!p2)
            continue;
        *p2 = 0;

        if (GetFileAttributesW(p2 + 1) == INVALID_FILE_ATTRIBUTES)
            continue;

        if (n >= cap) {
            int nc = cap ? cap * 2 : 64;
            RecentEntry *na = (RecentEntry *)realloc(arr, (size_t)nc * sizeof(RecentEntry));
            if (!na)
                break;
            arr = na;
            cap = nc;
        }
        memset(&arr[n], 0, sizeof(arr[n]));
        arr[n].count = _wtoi(line);
        arr[n].lastPlayed = _wtoi64(p1 + 1);
        wcsncpy(arr[n].path, p2 + 1, TRACK_PATH_MAX - 1);
        arr[n].path[TRACK_PATH_MAX - 1] = 0;

        playlist_add(pl, arr[n].path);   /* 记录顺序即"最近在前" */
        n++;
    }
    fclose(f);

    if (entries)
        *entries = arr;
    else
        free(arr);
    return n;
}

int store_save_recent(const Playlist *pl, const RecentEntry *entries, int n)
{
    wchar_t path[TRACK_PATH_MAX];
    FILE *f;
    int i;

    join(path, TRACK_PATH_MAX, g_dir, L"recent.txt");
    f = _wfopen(path, L"w, ccs=UTF-8");
    if (!f)
        return 0;

    fwprintf(f, L"# 次数|最后播放时间(unix)|路径\n");
    for (i = 0; i < n && i < pl->count; i++) {
        /* entries 与 pl 一一对应；没有 entries 时按 pl 原样写 */
        int cnt = entries ? entries[i].count : 0;
        long long t = entries ? entries[i].lastPlayed : 0;
        fwprintf(f, L"%d|%lld|%ls\n", cnt, t, pl->items[i].path);
    }
    fclose(f);
    return 1;
}

int store_touch_recent(Playlist *pl, RecentEntry **entries, int *n,
                       const wchar_t *path)
{
    RecentEntry *arr = (entries ? *entries : NULL);
    int count = (n ? *n : 0);
    int i, found = -1;
    long long now = (long long)time(NULL);
    RecentEntry *na;

    for (i = 0; i < count; i++) {
        if (_wcsicmp(arr[i].path, path) == 0) {
            found = i;
            break;
        }
    }

    na = (RecentEntry *)realloc(arr, (size_t)(count + 1) * sizeof(RecentEntry));
    if (!na)
        return 0;
    arr = na;

    if (found >= 0) {
        RecentEntry tmp = arr[found];
        memmove(&arr[1], &arr[0], (size_t)found * sizeof(RecentEntry));
        arr[0] = tmp;
        arr[0].count++;
        arr[0].lastPlayed = now;
    } else {
        memmove(&arr[1], &arr[0], (size_t)count * sizeof(RecentEntry));
        memset(&arr[0], 0, sizeof(arr[0]));
        wcsncpy(arr[0].path, path, TRACK_PATH_MAX - 1);
        arr[0].path[TRACK_PATH_MAX - 1] = 0;
        arr[0].count = 1;
        arr[0].lastPlayed = now;
        count++;
    }

    /* 最近播放只保留前 200 条 */
    if (count > 200)
        count = 200;

    /* 同步到 Playlist */
    playlist_clear(pl);
    for (i = 0; i < count; i++)
        playlist_add(pl, arr[i].path);

    if (entries) *entries = arr;
    if (n) *n = count;
    return 1;
}

/* ---------------- 播放列表（m3u） ---------------- */

static void playlist_file(wchar_t *out, size_t cch, const wchar_t *name)
{
    wchar_t dir[TRACK_PATH_MAX];
    wchar_t file[STORE_NAME_MAX + 8];
    join(dir, TRACK_PATH_MAX, g_dir, L"playlists");
    swprintf(file, STORE_NAME_MAX + 8, L"%ls.m3u", name);
    join(out, cch, dir, file);
}

int store_list_playlists(wchar_t names[][STORE_NAME_MAX], int maxNames)
{
    wchar_t dir[TRACK_PATH_MAX], pat[TRACK_PATH_MAX];
    WIN32_FIND_DATAW fd;
    HANDLE h;
    int n = 0;

    join(dir, TRACK_PATH_MAX, g_dir, L"playlists");
    join(pat, TRACK_PATH_MAX, dir, L"*.m3u");

    h = FindFirstFileW(pat, &fd);
    if (h == INVALID_HANDLE_VALUE)
        return 0;

    do {
        wchar_t *dot;
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
            continue;
        if (n >= maxNames)
            break;
        wcsncpy(names[n], fd.cFileName, STORE_NAME_MAX - 1);
        names[n][STORE_NAME_MAX - 1] = 0;
        dot = wcsrchr(names[n], L'.');
        if (dot) *dot = 0;               /* 去掉 .m3u */
        n++;
    } while (FindNextFileW(h, &fd));

    FindClose(h);
    return n;
}

int store_load_playlist(const wchar_t *name, Playlist *pl)
{
    wchar_t path[TRACK_PATH_MAX];
    FILE *f;

    playlist_clear(pl);
    playlist_file(path, TRACK_PATH_MAX, name);
    f = _wfopen(path, L"r, ccs=UTF-8");
    if (!f)
        return 0;

    for (;;) {
        wchar_t line[LINE_MAX];
        if (!fgetws(line, LINE_MAX, f))
            break;
        chomp(line);
        if (line[0] == 0 || line[0] == L'#')
            continue;
        playlist_add(pl, line);
    }
    fclose(f);
    return pl->count;
}

int store_save_playlist(const wchar_t *name, const Playlist *pl)
{
    wchar_t path[TRACK_PATH_MAX];
    FILE *f;
    int i;

    playlist_file(path, TRACK_PATH_MAX, name);
    f = _wfopen(path, L"w, ccs=UTF-8");
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

int store_delete_playlist(const wchar_t *name)
{
    wchar_t path[TRACK_PATH_MAX];
    playlist_file(path, TRACK_PATH_MAX, name);
    return DeleteFileW(path) ? 1 : 0;
}

/* ================= 我喜欢的音乐（favorites.txt，一行一个路径） ================= */

int store_load_favorites(Playlist *pl)
{
    wchar_t path[TRACK_PATH_MAX];
    wchar_t line[TRACK_PATH_MAX];
    FILE *f;

    if (!pl) return 0;
    playlist_clear(pl);
    join(path, TRACK_PATH_MAX, g_dir, L"favorites.txt");
    f = _wfopen(path, L"r, ccs=UTF-8");
    if (!f) return 0;
    while (fgetws(line, TRACK_PATH_MAX, f)) {
        chomp(line);
        if (line[0])
            playlist_add(pl, line);
    }
    fclose(f);
    return pl->count;
}

int store_save_favorites(const Playlist *pl)
{
    wchar_t path[TRACK_PATH_MAX];
    FILE *f;
    int i;

    if (!pl) return 0;
    join(path, TRACK_PATH_MAX, g_dir, L"favorites.txt");
    f = _wfopen(path, L"w, ccs=UTF-8");
    if (!f) return 0;
    for (i = 0; i < pl->count; i++)
        fwprintf(f, L"%ls\n", pl->items[i].path);
    fclose(f);
    return 1;
}
/* app.c —— 应用逻辑：视图管理、曲库、播放列表、最近播放、歌词、播放控制 */
#include "app.h"
#include "util.h"
#include "nav.h"
#include "lyricview.h"

#include <commdlg.h>
#include <shlobj.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>
#include <wctype.h>
#include <time.h>

App g_app;

const wchar_t *const g_mode_names[4] = {
    L"顺序播放", L"列表循环", L"单曲循环", L"随机播放"
};

static const wchar_t FILTER_AUDIO[] =
    L"音频文件 (*.mp3;*.wav;*.flac;*.m4a;*.wma)\0"
    L"*.mp3;*.wav;*.flac;*.m4a;*.wma;*.aac;*.mp2;*.aif;*.aiff;*.ogg\0"
    L"MP3 文件 (*.mp3)\0*.mp3\0"
    L"所有文件 (*.*)\0*.*\0";

static const wchar_t FILTER_M3U[] =
    L"播放列表 (*.m3u)\0*.m3u\0"
    L"所有文件 (*.*)\0*.*\0";

/* ================= 小工具 ================= */

static void pl_copy(Playlist *dst, const Playlist *src)
{
    int i;
    playlist_clear(dst);
    for (i = 0; i < src->count; i++)
        playlist_append_track(dst, &src->items[i]);   /* 整条复制，省掉重新读 ID3 */
}

/* 曲目时长学到之后，把各处的副本都更新一下 */
static void set_length_everywhere(App *app, const wchar_t *path, int ms)
{
    int i, j;
    Playlist *all[2 + MAX_USER_LISTS];
    int n = 0;

    all[n++] = &app->pl;
    all[n++] = &app->library;
    for (i = 0; i < app->listCount; i++)
        all[n++] = &app->lists[i].pl;

    for (i = 0; i < n; i++) {
        Playlist *p = all[i];
        j = playlist_find(p, path);
        if (j >= 0)
            p->items[j].length_ms = ms;
    }
}

/* ================= 视图 ================= */

/* 不加过滤时，当前视图对应的源列表 */
static Playlist *base_pl(App *app)
{
    switch (app->view) {
    case VIEW_LIBRARY: return &app->library;
    case VIEW_RECENT:    return &app->recent;
    case VIEW_FAVORITES: return &app->favorites;
    case VIEW_PLAYLIST:
        if (app->curList >= 0 && app->curList < app->listCount)
            return &app->lists[app->curList].pl;
        return &app->library;
    case VIEW_ARTISTS:
    case VIEW_ALBUMS:
    case VIEW_PLAYLISTS:
    default:
        return &app->library;
    }
}

Playlist *app_view_pl(App *app)
{
    if (app->view == VIEW_ARTISTS || app->view == VIEW_ALBUMS)
        return &app->library;          /* 分组视图不显示曲目 */
    if (app->filterActive)
        return &app->filtered;
    return base_pl(app);
}

const wchar_t *app_view_name(App *app)
{
    switch (app->view) {
    case VIEW_LIBRARY:   return L"我的歌曲";
    case VIEW_ARTISTS:   return L"歌手";
    case VIEW_ALBUMS:    return L"专辑";
    case VIEW_RECENT:    return L"最近播放";
    case VIEW_FAVORITES: return L"我喜欢的音乐";
    case VIEW_NOWPLAYING:return L"正在播放";
    case VIEW_PLAYLISTS: return L"播放列表";
    case VIEW_PLAYLIST:
        if (app->curList >= 0 && app->curList < app->listCount)
            return app->lists[app->curList].name;
        return L"播放列表";
    }
    return L"";
}

/* 视图 -> 导航下标（顺序：我的歌曲 歌手 专辑 播放列表 [各列表] 最近播放） */
static int nav_index_of_view(App *app)
{
    switch (app->view) {
    case VIEW_LIBRARY:   return 0;
    case VIEW_ARTISTS:   return 1;
    case VIEW_ALBUMS:    return 2;
    case VIEW_PLAYLISTS: return 3;
    case VIEW_PLAYLIST:  return 4 + (app->curList < 0 ? 0 : app->curList);
    case VIEW_RECENT:    return 4 + app->listCount;
    case VIEW_FAVORITES: return 5 + app->listCount;
    case VIEW_NOWPLAYING:return 6 + app->listCount;
    }
    return 0;
}

void app_set_view(App *app, ViewId v, int listIdx)
{
    app->view = v;
    app->curList = listIdx;

    /* 从分组视图离开时清掉分组过滤（搜索保留） */
    if (v != VIEW_LIBRARY) {
        app->filterKind = FILTER_NONE;
        app->filterText[0] = 0;
    }
    app_apply_filter(app);

    nav_set_sel(app->hNav, nav_index_of_view(app));
    store_set_int(L"lastList", listIdx);
    app_refresh_view(app);
}

/* 重建左侧导航条目 */
void app_rebuild_nav(App *app)
{
    NavItem items[NAV_MAX_ITEMS];
    int n = 0, i;

    ZeroMemory(items, sizeof(items));

    wcscpy(items[n].text, L"我的歌曲"); items[n].icon = NAV_ICON_SONG;  items[n].level = 0; n++;
    wcscpy(items[n].text, L"歌手");     items[n].icon = NAV_ICON_MIC;   items[n].level = 0; n++;
    wcscpy(items[n].text, L"专辑");     items[n].icon = NAV_ICON_DISC;  items[n].level = 0; n++;
    wcscpy(items[n].text, L"播放列表"); items[n].icon = NAV_ICON_LIST;  items[n].level = 0; n++;
    items[n - 1].gap = 1;

    for (i = 0; i < app->listCount && n < NAV_MAX_ITEMS; i++) {
        wcsncpy(items[n].text, app->lists[i].name, NAV_TEXT_MAX - 1);
        items[n].text[NAV_TEXT_MAX - 1] = 0;
        items[n].icon = NAV_ICON_PLAYLIST;
        items[n].level = 1;
        n++;
    }

    if (n < NAV_MAX_ITEMS) {
        wcscpy(items[n].text, L"最近播放");
        items[n].icon = NAV_ICON_CLOCK;
        items[n].level = 0;
        items[n].gap = 1;
        n++;
    }
    if (n < NAV_MAX_ITEMS) {
        wcscpy(items[n].text, L"我喜欢的音乐");
        items[n].icon = NAV_ICON_HEART;
        items[n].level = 0;
        n++;
    }
    if (n < NAV_MAX_ITEMS) {
        wcscpy(items[n].text, L"正在播放");
        items[n].icon = NAV_ICON_PLAY;
        items[n].level = 0;
        n++;
    }

    nav_set_items(app->hNav, items, n);
    nav_set_sel(app->hNav, nav_index_of_view(app));
}

/* ================= 分组与过滤 ================= */

static int contains_i(const wchar_t *hay, const wchar_t *needle)
{
    size_t nl;
    if (!needle || !*needle)
        return 1;
    if (!hay)
        return 0;
    nl = wcslen(needle);
    for (; *hay; hay++) {
        size_t k = 0;
        while (k < nl && hay[k] &&
               towlower((wint_t)hay[k]) == towlower((wint_t)needle[k]))
            k++;
        if (k == nl)
            return 1;
    }
    return 0;
}

/* 按歌手或专辑汇总曲库 */
void app_build_groups(App *app, int byAlbum)
{
    int i, j;

    app->groupCount = 0;
    app->coverDirty = 1;          /* 分组变了，封面要重做 */
    for (i = 0; i < app->library.count; i++) {
        Track *t = &app->library.items[i];
        const wchar_t *key = byAlbum ? t->album : t->artist;

        if (!key[0])
            key = L"未知";

        for (j = 0; j < app->groupCount; j++) {
            if (_wcsicmp(app->groups[j].name, key) == 0) {
                app->groups[j].count++;
                break;
            }
        }
        if (j == app->groupCount && app->groupCount < MAX_GROUPS) {
            wcsncpy(app->groups[app->groupCount].name, key, GROUP_NAME_MAX - 1);
            app->groups[app->groupCount].name[GROUP_NAME_MAX - 1] = 0;
            app->groups[app->groupCount].count = 1;
            app->groupCount++;
        }
    }

    /* 按名称排序（简单的插入排序，分组数量不大） */
    for (i = 1; i < app->groupCount; i++) {
        Group key = app->groups[i];
        j = i - 1;
        while (j >= 0 && _wcsicmp(app->groups[j].name, key.name) > 0) {
            app->groups[j + 1] = app->groups[j];
            j--;
        }
        app->groups[j + 1] = key;
    }
}

void app_open_group(App *app, int index)
{
    if (index < 0 || index >= app->groupCount)
        return;

    app->filterKind = (app->view == VIEW_ALBUMS) ? FILTER_ALBUM : FILTER_ARTIST;
    wcsncpy(app->filterText, app->groups[index].name, GROUP_NAME_MAX - 1);
    app->filterText[GROUP_NAME_MAX - 1] = 0;
    app->view = VIEW_LIBRARY;          /* 下钻后当作"我的歌曲"的过滤结果 */
    app_apply_filter(app);
    nav_set_sel(app->hNav, 0);
    app_refresh_view(app);
}

void app_clear_filter(App *app)
{
    app->filterKind = FILTER_NONE;
    app->filterText[0] = 0;
    app->searchText[0] = 0;
    if (app->hSearch)
        SetWindowTextW(app->hSearch, L"");
    app_apply_filter(app);
    app_refresh_view(app);
}

void app_set_search(App *app, const wchar_t *text)
{
    wcsncpy(app->searchText, text ? text : L"", GROUP_NAME_MAX - 1);
    app->searchText[GROUP_NAME_MAX - 1] = 0;
    app_apply_filter(app);
    app_refresh_view(app);
}

/* 把"分组过滤 + 搜索"应用到当前视图的源列表，结果放进 app->filtered */
void app_apply_filter(App *app)
{
    Playlist *src = base_pl(app);
    int i;

    app->filterActive = 0;
    playlist_clear(&app->filtered);

    if (app->view == VIEW_ARTISTS || app->view == VIEW_ALBUMS)
        return;
    if (app->filterKind == FILTER_NONE && app->searchText[0] == 0)
        return;

    for (i = 0; i < src->count; i++) {
        Track *t = &src->items[i];

        if (app->filterKind == FILTER_ARTIST &&
            _wcsicmp(t->artist[0] ? t->artist : L"未知", app->filterText) != 0)
            continue;
        if (app->filterKind == FILTER_ALBUM &&
            _wcsicmp(t->album[0] ? t->album : L"未知", app->filterText) != 0)
            continue;
        if (app->searchText[0]) {
            if (!contains_i(t->title, app->searchText) &&
                !contains_i(t->artist, app->searchText) &&
                !contains_i(t->album, app->searchText) &&
                !contains_i(util_basename(t->path), app->searchText))
                continue;
        }
        playlist_append_track(&app->filtered, t);
    }
    app->filterActive = 1;
}


void app_refresh_view(App *app)
{
    ui_refresh_list(app);
    ui_scroll_to_playing(app);
    ui_update_status(app, NULL);
}

/* ================= 初始化 / 释放 ================= */

void app_init(App *app, HINSTANCE hInst, HWND hMain)
{
    int dpi = app->dpi;
    int i;

    memset(app, 0, sizeof(*app));
    app->dpi = dpi ? dpi : 96;
    app->hInst = hInst;
    app->hMain = hMain;
    app->current = -1;
    app->mode = MODE_SEQUENTIAL;
    app->volume = 80;
    app->lastRandom = -1;
    app->rng = (unsigned)time(NULL);
    app->showLyric = 1;

    playlist_init(&app->pl);
    playlist_init(&app->library);
    playlist_init(&app->recent);
    playlist_init(&app->favorites);
    playlist_init(&app->filtered);
    app->filterKind = FILTER_NONE;
    app->filterText[0] = 0;
    app->searchText[0] = 0;
    app->filterActive = 0;
    for (i = 0; i < MAX_USER_LISTS; i++)
        playlist_init(&app->lists[i].pl);
    lrc_init(&app->lrc);

    app->audio = audio_create(hMain);

    /* 载入持久化数据（store_init 必须先跑，它负责建 data\ 目录） */
    store_init();
    store_load_config();
    app->volume = store_get_int(L"volume", 80);
    app->mode   = (PlayMode)store_get_int(L"mode", MODE_SEQUENTIAL);
    app->showLyric = store_get_int(L"showLyric", 1);
    if (app->volume < 0 || app->volume > 100) app->volume = 80;
    if ((int)app->mode < 0 || (int)app->mode > 3) app->mode = MODE_SEQUENTIAL;

    store_load_library(&app->library);
    store_load_recent(&app->recent, &app->recentMeta);
    store_load_favorites(&app->favorites);
    app->recentCount = app->recent.count;

    {
        wchar_t names[MAX_USER_LISTS][STORE_NAME_MAX];
        int k = store_list_playlists(names, MAX_USER_LISTS);
        app->listCount = k;
        for (i = 0; i < k; i++) {
            wcsncpy(app->lists[i].name, names[i], STORE_NAME_MAX - 1);
            app->lists[i].name[STORE_NAME_MAX - 1] = 0;
            store_load_playlist(app->lists[i].name, &app->lists[i].pl);
        }
    }

    /* 每次打开都从"我的歌曲"开始（用户要求：打开显示我的歌曲） */
    app->view = VIEW_LIBRARY;
    app->curList = 0;
    store_set_int(L"lastView", (int)VIEW_LIBRARY);
}

void app_free(App *app)
{
    int i;

    store_set_int(L"volume", app->volume);
    store_set_int(L"mode", (int)app->mode);
    store_set_int(L"showLyric", app->showLyric);
    store_save_config();

    store_save_library(&app->library);
    store_save_recent(&app->recent, app->recentMeta, app->recentCount);

    if (app->audio) {
        audio_destroy(app->audio);
        app->audio = NULL;
    }

    playlist_free(&app->pl);
    playlist_free(&app->library);
    playlist_free(&app->recent);
    playlist_free(&app->favorites);
    playlist_free(&app->filtered);
    for (i = 0; i < app->listCount; i++)
        playlist_free(&app->lists[i].pl);

    free(app->recentMeta);
    app->recentMeta = NULL;
    lrc_free(&app->lrc);
}

/* ================= 歌词 ================= */

void app_load_lyric_for_current(App *app)
{
    lrc_free(&app->lrc);
    app->lrcValid = 0;

    if (app->hLyric)
        lyricview_set_lrc(app->hLyric, NULL, NULL);

    if (app->current >= 0 && app->current < app->pl.count) {
        /* 先用用户手动指定过的歌词文件（存在 config 的 lrc_<文件名> 里） */
        wchar_t key[160];
        const wchar_t *base = util_basename(app->pl.items[app->current].path);
        const wchar_t *manual;

        swprintf(key, 160, L"lrc_%ls", base);
        manual = store_get_str(key);

        if (manual && manual[0]) {
            if (lrc_load_for(&app->lrc, manual) > 0)
                app->lrcValid = 1;
        }
        if (!app->lrcValid &&
            lrc_load_for(&app->lrc, app->pl.items[app->current].path) > 0)
            app->lrcValid = 1;
        if (app->hLyric)
            lyricview_set_lrc(app->hLyric, &app->lrc,
                              app->pl.items[app->current].title);
    }
}

/* ================= 播放控制 ================= */

static unsigned next_rand(App *app)
{
    app->rng = app->rng * 1103515245u + 12345u;
    return (app->rng >> 16) & 0x7fff;
}

static int random_index(App *app)
{
    int n = app->pl.count;
    int idx, guard = 0;
    if (n <= 1) return 0;
    do {
        idx = (int)(next_rand(app) % (unsigned)n);
        guard++;
    } while (idx == app->lastRandom && guard < 32);
    app->lastRandom = idx;
    return idx;
}

int app_play_index(App *app, int idx)
{
    Track *t;
    int len = -1, row;

    if (idx < 0 || idx >= app->pl.count)
        return 0;

    t = &app->pl.items[idx];

    if (!audio_open(app->audio, t->path)) {
        ui_update_status(app, audio_last_error(app->audio));
        return 0;
    }

    audio_set_volume(app->audio, app->volume);
    app->current = idx;
    if (app->mode == MODE_SHUFFLE)
        app->lastRandom = idx;

    audio_play(app->audio);

    if (audio_get_length(app->audio, &len) && len > 0) {
        t->length_ms = len;
        set_length_everywhere(app, t->path, len);
    }

    /* 记进"最近播放" */
    store_touch_recent(&app->recent, &app->recentMeta, &app->recentCount, t->path);
    app->recentCount = app->recent.count;
    store_save_recent(&app->recent, app->recentMeta, app->recentCount);

    /* 歌词 */
    app_load_lyric_for_current(app);

    /* 界面：最近播放视图的顺序变了要整体重建，其它视图只改当前行 */
    if (app->view == VIEW_RECENT)
        ui_refresh_list(app);
    else {
        row = ui_find_row_of_path(app, t->path);
        if (row >= 0)
            ui_update_row(app, row);
    }
    ui_sync_selection(app);
    ui_update_times(app, 0, t->length_ms);
    ui_update_progress(app);
    ui_update_play_button(app);
    ui_update_status(app, NULL);
    return 1;
}

/* 双击某个视图的第 row 行：把该视图整个作为播放队列 */
int app_play_from_view(App *app, int row)
{
    Playlist *vpl;
    const wchar_t *path;

    /* 分组视图：双击 = 下钻到该歌手/专辑 */
    if (app->view == VIEW_ARTISTS || app->view == VIEW_ALBUMS) {
        app_open_group(app, row);
        return 1;
    }

    if (app->view == VIEW_PLAYLISTS) {
        /* 总览页双击 = 打开那个播放列表 */
        if (row >= 0 && row < app->listCount) {
            app_set_view(app, VIEW_PLAYLIST, row);
            return 1;
        }
        return 0;
    }

    vpl = app_view_pl(app);
    if (row < 0 || row >= vpl->count)
        return 0;

    path = vpl->items[row].path;

    pl_copy(&app->pl, vpl);              /* 队列 = 当前视图（含过滤结果） */
    {
        int i = playlist_find(&app->pl, path);
        if (i < 0) i = row;
        return app_play_index(app, i);
    }
}

/* 播放当前视图的全部（从头） */
void app_play_all_visible(App *app)
{
    Playlist *vpl = app_view_pl(app);
    if (vpl->count <= 0)
        return;
    pl_copy(&app->pl, vpl);
    app_play_index(app, 0);
}

void app_toggle_play(App *app)
{
    int pos = 0, len = 0;

    if (app->pl.count == 0) {
        app_open_files(app);
        return;
    }
    if (app->current < 0) {
        app_play_index(app, 0);
        return;
    }
    if (audio_is_playing(app->audio)) {
        audio_pause(app->audio);
    } else {
        audio_get_position(app->audio, &pos);
        audio_get_length(app->audio, &len);
        if (len > 0 && pos >= len - 300) {
            app_play_index(app, app->current);
            return;
        }
        if (!audio_resume(app->audio))
            audio_play(app->audio);
    }
    ui_update_play_button(app);
}

void app_stop(App *app)
{
    audio_stop(app->audio);
    ui_update_progress(app);
    ui_update_play_button(app);
}

void app_next(App *app, int userInit)
{
    int n = app->pl.count, idx;

    if (n == 0) return;

    switch (app->mode) {
    case MODE_LOOP_ONE:
        if (!userInit) {
            app_play_index(app, app->current < 0 ? 0 : app->current);
            return;
        }
        idx = (app->current + 1) % n;
        break;
    case MODE_SHUFFLE:
        idx = random_index(app);
        break;
    case MODE_LOOP_ALL:
        idx = (app->current + 1) % n;
        break;
    case MODE_SEQUENTIAL:
    default:
        idx = app->current + 1;
        if (idx >= n) { app_stop(app); return; }
        break;
    }
    app_play_index(app, idx);
}

void app_prev(App *app)
{
    int n = app->pl.count, idx;
    if (n == 0) return;

    if (app->mode == MODE_SHUFFLE) {
        idx = random_index(app);
    } else {
        idx = app->current - 1;
        if (idx < 0)
            idx = (app->mode == MODE_LOOP_ALL) ? n - 1 : 0;
    }
    app_play_index(app, idx);
}

void app_set_mode(App *app, PlayMode m)
{
    app->mode = m;
    store_set_int(L"mode", (int)m);
    ui_update_mode_button(app);
    ui_update_status(app, NULL);
}

void app_set_volume(App *app, int vol)
{
    app->volume = vol;
    store_set_int(L"volume", vol);
    audio_set_volume(app->audio, vol);
    ui_update_status(app, NULL);
}

void app_on_tick(App *app)
{
    int playing;

    if (audio_take_ended(app->audio)) {
        app_next(app, 0);
        return;
    }

    playing = audio_is_playing(app->audio);
    if (playing != app->shownPlaying) {
        app->shownPlaying = playing;
        ui_update_play_button(app);
        ui_update_status(app, NULL);
    }

    ui_update_progress(app);

    /* 歌词跟着进度走 */
    {
        int pos = 0;
        if (app->current >= 0 && audio_get_position(app->audio, &pos))
            ui_update_lyric(app, 0);
    }
}

/* ================= 曲库 ================= */

void app_open_files(App *app)
{
    OPENFILENAMEW ofn;
    wchar_t buf[32768];
    wchar_t dir[TRACK_PATH_MAX];
    wchar_t *p;
    int added = 0;

    ZeroMemory(&ofn, sizeof(ofn));
    buf[0] = 0;

    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner   = app->hMain;
    ofn.lpstrFilter = FILTER_AUDIO;
    ofn.lpstrFile   = buf;
    ofn.nMaxFile    = (DWORD)(sizeof(buf) / sizeof(buf[0]));
    ofn.lpstrTitle  = L"打开音频文件";
    ofn.Flags = OFN_EXPLORER | OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST |
                OFN_ALLOWMULTISELECT | OFN_HIDEREADONLY;

    if (!GetOpenFileNameW(&ofn))
        return;

    wcsncpy(dir, buf, TRACK_PATH_MAX);
    dir[TRACK_PATH_MAX - 1] = 0;

    p = buf + wcslen(buf) + 1;
    if (*p == 0) {
        if (playlist_add(&app->library, dir) >= 0) added++;
    } else {
        while (*p) {
            wchar_t full[TRACK_PATH_MAX];
            swprintf(full, TRACK_PATH_MAX, L"%ls\\%ls", dir, p);
            if (playlist_add(&app->library, full) >= 0) added++;
            p += wcslen(p) + 1;
        }
    }

    if (added) {
        store_save_library(&app->library);
        ui_update_status(app, L"已加入我的歌曲");
    } else {
        ui_update_status(app, L"没有新增（可能已在曲库中）");
    }
    if (app->view == VIEW_LIBRARY)
        ui_refresh_list(app);
}

void app_add_folder(App *app)
{
    BROWSEINFOW bi;
    LPITEMIDLIST pidl;
    wchar_t dir[MAX_PATH];
    int before, added;

    ZeroMemory(&bi, sizeof(bi));
    bi.hwndOwner = app->hMain;
    bi.lpszTitle = L"选择音乐文件夹（含子文件夹）";
    bi.ulFlags   = BIF_RETURNONLYFSDIRS | BIF_NEWDIALOGSTYLE;

    pidl = SHBrowseForFolderW(&bi);
    if (!pidl) return;

    dir[0] = 0;
    if (SHGetPathFromIDListW(pidl, dir) && dir[0]) {
        before = app->library.count;
        library_scan_dir(&app->library, dir, 1);
        added = app->library.count - before;
        store_save_library(&app->library);
        if (app->view == VIEW_LIBRARY)
            ui_refresh_list(app);
        ui_update_status(app, added ? L"已加入我的歌曲" : L"该文件夹里没找到音频文件");
    }
    CoTaskMemFree(pidl);
}

/* 扫描系统"音乐"文件夹 */
void app_scan_music_folder(App *app)
{
    wchar_t dir[MAX_PATH];
    int before, added = 0;
    const wchar_t *cands[2];

    /* 常见位置：%USERPROFILE%\Music 与 %USERPROFILE%\音乐 */
    if (SUCCEEDED(SHGetFolderPathW(NULL, CSIDL_MYMUSIC, NULL, 0, dir)) && dir[0]) {
        before = app->library.count;
        library_scan_dir(&app->library, dir, 1);
        added += app->library.count - before;
    }
    {
        wchar_t alt[MAX_PATH];
        swprintf(alt, MAX_PATH, L"%ls\\音乐", _wgetenv(L"USERPROFILE") ? _wgetenv(L"USERPROFILE") : L"");
        if (GetFileAttributesW(alt) != INVALID_FILE_ATTRIBUTES) {
            before = app->library.count;
            library_scan_dir(&app->library, alt, 1);
            added += app->library.count - before;
        }
    }
    (void)cands;

    store_save_library(&app->library);
    if (app->view == VIEW_LIBRARY)
        ui_refresh_list(app);

    {
        wchar_t msg[256];
        swprintf(msg, 256, L"扫描完成，新增 %d 首，曲库共 %d 首", added, app->library.count);
        ui_update_status(app, msg);
    }
}

/* 命令行/拖拽进来：加进曲库并播放 */
int app_open_paths(App *app, const wchar_t *cmdline)
{
    const wchar_t *p = cmdline;
    int added = 0, first = -1;

    if (!cmdline) return 0;

    while (*p) {
        wchar_t buf[TRACK_PATH_MAX];
        const wchar_t *s;
        size_t n;
        DWORD attr;

        while (*p == L' ' || *p == L'\t') p++;
        if (!*p) break;

        if (*p == L'"') { p++; s = p; while (*p && *p != L'"') p++; }
        else            { s = p; while (*p && *p != L' ' && *p != L'\t') p++; }

        n = (size_t)(p - s);
        if (*p == L'"') p++;
        if (n == 0 || n >= TRACK_PATH_MAX) continue;

        wcsncpy(buf, s, n);
        buf[n] = 0;

        if (buf[0] == L'-') continue;

        attr = GetFileAttributesW(buf);
        if (attr == INVALID_FILE_ATTRIBUTES) continue;

        if (attr & FILE_ATTRIBUTE_DIRECTORY) {
            int before = app->library.count;
            library_scan_dir(&app->library, buf, 1);
            added += app->library.count - before;
        } else if (util_is_audio_file(buf)) {
            int idx = playlist_add(&app->library, buf);
            if (idx >= 0) {
                if (first < 0) first = idx;
                added++;
            }
        }
    }

    if (added) {
        store_save_library(&app->library);
        ui_refresh_list(app);
        ui_update_status(app, NULL);
        if (first >= 0) {                 /* 拖进来的是具体文件 -> 直接播 */
            pl_copy(&app->pl, &app->library);
            app_play_index(app, first);
        }
    }
    return added;
}

/* ================= 移除 / 清空 ================= */

void app_remove_selected(App *app)
{
    int i, sel, removedCurrent = 0;

    sel = (int)SendMessageW(app->hList, LVM_GETSELECTEDCOUNT, 0, 0);
    if (sel <= 0) return;

    if (app->view == VIEW_PLAYLISTS) {
        app_delete_current_playlist(app);
        return;
    }

    {
        Playlist *vpl = app_view_pl(app);
        Playlist *target = app->filterActive ? base_pl(app) : vpl;
        for (i = vpl->count - 1; i >= 0; i--) {
            if (ListView_GetItemState(app->hList, i, LVIS_SELECTED) & LVIS_SELECTED) {
                const wchar_t *path = vpl->items[i].path;
                int k;
                if (app->current >= 0 && app->current < app->pl.count &&
                    _wcsicmp(app->pl.items[app->current].path, path) == 0)
                    removedCurrent = 1;
                /* 有搜索/分组过滤时，要从真正的源列表里删 */
                k = playlist_find(target, path);
                if (k >= 0)
                    playlist_remove(target, k);
            }
        }
    }

    if (removedCurrent) {
        audio_stop(app->audio);
        audio_close(app->audio);
        app->current = -1;
    }

    if (app->view == VIEW_LIBRARY)
        store_save_library(&app->library);
    else if (app->view == VIEW_PLAYLIST && app->curList < app->listCount)
        store_save_playlist(app->lists[app->curList].name, &app->lists[app->curList].pl);
    else if (app->view == VIEW_RECENT)
        store_save_recent(&app->recent, app->recentMeta, app->recentCount);

    ui_refresh_list(app);
    ui_update_play_button(app);
    ui_update_progress(app);
    ui_update_status(app, NULL);
}

void app_clear_library(App *app)
{
    playlist_clear(&app->library);
    store_save_library(&app->library);
    ui_refresh_list(app);
    ui_update_status(app, L"曲库已清空");
}

/* ================= 播放列表操作 ================= */

typedef struct InputParams {
    wchar_t       *buf;
    int            cch;
    const wchar_t *prompt;
    const wchar_t *title;
    int            ok;
} InputParams;

static INT_PTR CALLBACK input_dlgproc(HWND hDlg, UINT msg, WPARAM wp, LPARAM lp)
{
    InputParams *p = (InputParams *)GetWindowLongPtrW(hDlg, DWLP_USER);

    switch (msg) {
    case WM_INITDIALOG:
        p = (InputParams *)lp;
        SetWindowLongPtrW(hDlg, DWLP_USER, (LONG_PTR)p);
        if (p) {
            if (p->title)
                SetWindowTextW(hDlg, p->title);
            if (p->prompt)
                SetDlgItemTextW(hDlg, IDC_INPUT_PROMPT, p->prompt);
            SetDlgItemTextW(hDlg, IDC_INPUT_EDIT, p->buf);
            SendDlgItemMessageW(hDlg, IDC_INPUT_EDIT, EM_SETSEL, 0, -1);
            SetFocus(GetDlgItem(hDlg, IDC_INPUT_EDIT));
        }
        return FALSE;   /* 由我们自己设置焦点 */

    case WM_COMMAND:
        switch (LOWORD(wp)) {
        case IDOK:
            if (p) {
                GetDlgItemTextW(hDlg, IDC_INPUT_EDIT, p->buf, p->cch);
                p->ok = 1;
            }
            EndDialog(hDlg, IDOK);
            return TRUE;
        case IDCANCEL:
            EndDialog(hDlg, IDCANCEL);
            return TRUE;
        }
        return FALSE;
    }
    return FALSE;
}

int app_input_box(HWND owner, const wchar_t *title, const wchar_t *prompt,
                  wchar_t *buf, int cch)
{
    InputParams p;

    p.buf = buf; p.cch = cch;
    p.prompt = prompt; p.title = title; p.ok = 0;

    DialogBoxParamW(GetModuleHandleW(NULL), MAKEINTRESOURCEW(IDD_INPUT),
                    owner, input_dlgproc, (LPARAM)&p);
    return p.ok;
}

void app_new_playlist(App *app)
{
    wchar_t name[STORE_NAME_MAX];
    wchar_t msg[256];

    if (app->listCount >= MAX_USER_LISTS) {
        MessageBoxW(app->hMain, L"播放列表数量已达上限", L"提示", MB_OK | MB_ICONINFORMATION);
        return;
    }

    name[0] = 0;
    if (!app_input_box(app->hMain, L"新建播放列表", L"播放列表名称：", name, STORE_NAME_MAX))
        return;
    if (!name[0]) return;

    /* 重名检查 */
    {
        int i;
        for (i = 0; i < app->listCount; i++) {
            if (_wcsicmp(app->lists[i].name, name) == 0) {
                swprintf(msg, 256, L"已经有一个叫「%ls」的播放列表了", name);
                MessageBoxW(app->hMain, msg, L"提示", MB_OK | MB_ICONINFORMATION);
                return;
            }
        }
    }

    wcsncpy(app->lists[app->listCount].name, name, STORE_NAME_MAX - 1);
    app->lists[app->listCount].name[STORE_NAME_MAX - 1] = 0;
    playlist_clear(&app->lists[app->listCount].pl);
    store_save_playlist(name, &app->lists[app->listCount].pl);
    app->listCount++;

    app_rebuild_nav(app);
    app_set_view(app, VIEW_PLAYLIST, app->listCount - 1);
}

/* 把当前视图里选中的歌加进某个播放列表 */
void app_add_selected_to_playlist(App *app)
{
    wchar_t picked[STORE_NAME_MAX];
    wchar_t msg[256];
    Playlist *vpl;
    int i, n, added = 0;
    int selCount;

    if (app->listCount == 0) {
        if (MessageBoxW(app->hMain, L"还没有播放列表，现在新建一个吗？", L"添加到播放列表",
                        MB_YESNO | MB_ICONQUESTION) == IDYES)
            app_new_playlist(app);
        return;
    }

    vpl = app_view_pl(app);
    selCount = (int)SendMessageW(app->hList, LVM_GETSELECTEDCOUNT, 0, 0);
    if (selCount <= 0) {
        ui_update_status(app, L"先在列表里选几首歌再添加到播放列表");
        return;
    }

    /* 让用户挑一个播放列表：用一个简单的选择对话框太啰嗦，
       这里如果只有一个就直接用，否则弹输入框让用户输名字。 */
    if (app->listCount == 1) {
        wcsncpy(picked, app->lists[0].name, STORE_NAME_MAX - 1);
        picked[STORE_NAME_MAX - 1] = 0;
    } else {
        wchar_t hint[1024];
        hint[0] = 0;
        for (i = 0; i < app->listCount; i++) {
            wcscat(hint, L"  • ");
            wcscat(hint, app->lists[i].name);
            wcscat(hint, L"\n");
        }
        {
            wchar_t prompt[1200];
            swprintf(prompt, 1200, L"要添加到哪个播放列表？\n\n已有的：\n%ls", hint);
            picked[0] = 0;
            if (!app_input_box(app->hMain, L"添加到播放列表", prompt, picked, STORE_NAME_MAX))
                return;
        }
    }

    n = -1;
    for (i = 0; i < app->listCount; i++) {
        if (_wcsicmp(app->lists[i].name, picked) == 0) { n = i; break; }
    }
    if (n < 0) {
        swprintf(msg, 256, L"没有叫「%ls」的播放列表", picked);
        MessageBoxW(app->hMain, msg, L"提示", MB_OK | MB_ICONINFORMATION);
        return;
    }

    for (i = 0; i < vpl->count; i++) {
        if (ListView_GetItemState(app->hList, i, LVIS_SELECTED) & LVIS_SELECTED) {
            if (playlist_add(&app->lists[n].pl, vpl->items[i].path) >= 0)
                added++;
        }
    }

    store_save_playlist(app->lists[n].name, &app->lists[n].pl);
    swprintf(msg, 256, L"已添加 %d 首到「%ls」（跳过 %d 首重复的）",
             added, app->lists[n].name, selCount - added);
    ui_update_status(app, msg);
}

void app_delete_current_playlist(App *app)
{
    wchar_t msg[256];
    int i;

    if (app->view != VIEW_PLAYLIST || app->curList < 0 || app->curList >= app->listCount) {
        ui_update_status(app, L"先选中一个播放列表（左侧导航）再删除");
        return;
    }

    swprintf(msg, 256, L"确定要删除播放列表「%ls」吗？\n（只删列表，不删歌曲文件）",
             app->lists[app->curList].name);
    if (MessageBoxW(app->hMain, msg, L"删除播放列表",
                    MB_YESNO | MB_ICONWARNING) != IDYES)
        return;

    store_delete_playlist(app->lists[app->curList].name);
    playlist_free(&app->lists[app->curList].pl);

    for (i = app->curList; i < app->listCount - 1; i++)
        app->lists[i] = app->lists[i + 1];
    app->listCount--;

    app->view = VIEW_PLAYLISTS;
    app->curList = 0;
    app_rebuild_nav(app);
    app_refresh_view(app);
}

void app_rename_current_playlist(App *app)
{
    wchar_t name[STORE_NAME_MAX];

    if (app->view != VIEW_PLAYLIST || app->curList < 0 || app->curList >= app->listCount) {
        ui_update_status(app, L"先选中一个播放列表再重命名");
        return;
    }

    wcsncpy(name, app->lists[app->curList].name, STORE_NAME_MAX - 1);
    name[STORE_NAME_MAX - 1] = 0;
    if (!app_input_box(app->hMain, L"重命名播放列表", L"新的名称：", name, STORE_NAME_MAX))
        return;
    if (!name[0] || _wcsicmp(name, app->lists[app->curList].name) == 0)
        return;

    store_delete_playlist(app->lists[app->curList].name);
    wcsncpy(app->lists[app->curList].name, name, STORE_NAME_MAX - 1);
    app->lists[app->curList].name[STORE_NAME_MAX - 1] = 0;
    store_save_playlist(name, &app->lists[app->curList].pl);

    app_rebuild_nav(app);
    app_refresh_view(app);
}

/* 把当前播放列表导出成 m3u */
void app_save_current_playlist(App *app)
{
    OPENFILENAMEW ofn;
    wchar_t path[MAX_PATH];
    Playlist *vpl;

    if (app->view == VIEW_PLAYLIST && app->curList < app->listCount)
        vpl = &app->lists[app->curList].pl;
    else
        vpl = app_view_pl(app);

    if (vpl->count <= 0) {
        ui_update_status(app, L"当前列表是空的");
        return;
    }

    ZeroMemory(&ofn, sizeof(ofn));
    wcscpy(path, app_view_name(app));
    wcscat(path, L".m3u");

    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = app->hMain;
    ofn.lpstrFilter = FILTER_M3U;
    ofn.lpstrFile = path;
    ofn.nMaxFile = MAX_PATH;
    ofn.lpstrTitle = L"导出播放列表";
    ofn.lpstrDefExt = L"m3u";
    ofn.Flags = OFN_EXPLORER | OFN_OVERWRITEPROMPT | OFN_HIDEREADONLY;

    if (!GetSaveFileNameW(&ofn))
        return;

    if (playlist_save_m3u(path, vpl))
        ui_update_status(app, L"已导出播放列表");
    else
        ui_update_status(app, L"导出失败");
}

/* 从 m3u 文件导入成新播放列表 */
void app_load_playlist_file(App *app)
{
    OPENFILENAMEW ofn;
    wchar_t path[MAX_PATH];
    wchar_t name[STORE_NAME_MAX];
    const wchar_t *base;
    const wchar_t *dot;
    Playlist tmp;

    if (app->listCount >= MAX_USER_LISTS) {
        MessageBoxW(app->hMain, L"播放列表数量已达上限", L"提示", MB_OK | MB_ICONINFORMATION);
        return;
    }

    ZeroMemory(&ofn, sizeof(ofn));
    path[0] = 0;
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = app->hMain;
    ofn.lpstrFilter = FILTER_M3U;
    ofn.lpstrFile = path;
    ofn.nMaxFile = MAX_PATH;
    ofn.lpstrTitle = L"导入播放列表";
    ofn.Flags = OFN_EXPLORER | OFN_FILEMUSTEXIST | OFN_HIDEREADONLY;

    if (!GetOpenFileNameW(&ofn))
        return;

    base = util_basename(path);
    wcsncpy(name, base, STORE_NAME_MAX - 1);
    name[STORE_NAME_MAX - 1] = 0;
    dot = wcsrchr(name, L'.');
    if (dot) *(wchar_t *)dot = 0;

    playlist_init(&tmp);
    if (!playlist_load_m3u(path, &tmp) || tmp.count == 0) {
        playlist_free(&tmp);
        MessageBoxW(app->hMain, L"这个文件里没有能读取的歌曲", L"导入失败", MB_OK | MB_ICONWARNING);
        return;
    }

    {
        int i, k = app->listCount;
        wcsncpy(app->lists[k].name, name, STORE_NAME_MAX - 1);
        app->lists[k].name[STORE_NAME_MAX - 1] = 0;
        playlist_init(&app->lists[k].pl);
        for (i = 0; i < tmp.count; i++)
            playlist_add(&app->lists[k].pl, tmp.items[i].path);
        store_save_playlist(name, &app->lists[k].pl);
        app->listCount++;
    }
    playlist_free(&tmp);

    app_rebuild_nav(app);
    app_set_view(app, VIEW_PLAYLIST, app->listCount - 1);
}

void app_clear_recent(App *app)
{
    if (app->recent.count == 0) {
        ui_update_status(app, L"最近播放本来就是空的");
        return;
    }
    if (MessageBoxW(app->hMain, L"确定要清空最近播放记录吗？", L"清空记录",
                    MB_YESNO | MB_ICONQUESTION) != IDYES)
        return;

    playlist_clear(&app->recent);
    free(app->recentMeta);
    app->recentMeta = NULL;
    app->recentCount = 0;
    store_save_recent(&app->recent, NULL, 0);
    app_refresh_view(app);
    ui_update_status(app, L"最近播放已清空");
}

/* ================= 导航 / 歌词点击 ================= */

void app_on_nav_click(App *app, int index)
{
    if (index == 0) {
        app->view = VIEW_LIBRARY;
        app->filterKind = FILTER_NONE;
        app->filterText[0] = 0;
        app_apply_filter(app);
        nav_set_sel(app->hNav, 0);
        app_refresh_view(app);
    } else if (index == 1) {
        app_set_view(app, VIEW_ARTISTS, 0);
        app_build_groups(app, 0);
        app_refresh_view(app);
    } else if (index == 2) {
        app_set_view(app, VIEW_ALBUMS, 0);
        app_build_groups(app, 1);
        app_refresh_view(app);
    } else if (index == 3) {
        app_set_view(app, VIEW_PLAYLISTS, 0);
    } else if (index >= 4 && index - 4 < app->listCount) {
        app_set_view(app, VIEW_PLAYLIST, index - 4);
    } else if (index == 4 + app->listCount) {
        app_set_view(app, VIEW_RECENT, 0);
    } else if (index == 5 + app->listCount) {
        app_set_view(app, VIEW_FAVORITES, 0);
    } else {
        app_set_view(app, VIEW_NOWPLAYING, 0);
    }
}

void app_on_lyric_click(App *app, int line)
{
    if (!app->lrcValid || line < 0 || line >= app->lrc.count)
        return;
    if (app->current < 0)
        return;
    audio_seek(app->audio, app->lrc.lines[line].timeMs);
    ui_update_progress(app);
}

/* ================= 播放状态 & 收藏 ================= */

int app_is_playing(App *app)
{
    return (app && app->audio) ? audio_is_playing(app->audio) : 0;
}

int app_current_position(App *app)
{
    int ms = 0;
    if (app && app->audio)
        audio_get_position(app->audio, &ms);
    return ms;
}

int app_current_length(App *app)
{
    int ms = 0;
    if (app && app->audio)
        audio_get_length(app->audio, &ms);
    return ms;
}

int app_is_favorite(App *app, const wchar_t *path)
{
    if (!app || !path || !path[0])
        return 0;
    return playlist_find(&app->favorites, path) >= 0;
}

void app_np_refresh(App *app)
{
    if (app && app->hNowPlaying)
        InvalidateRect(app->hNowPlaying, NULL, FALSE);
}

void app_toggle_fav_path(App *app, const wchar_t *path)
{
    int i;

    if (!app || !path || !path[0])
        return;

    i = playlist_find(&app->favorites, path);
    if (i >= 0)
        playlist_remove(&app->favorites, i);
    else
        playlist_add(&app->favorites, path);

    store_save_favorites(&app->favorites);
    app_rebuild_nav(app);
    if (app->view == VIEW_FAVORITES)
        app_refresh_view(app);
    app_np_refresh(app);
}

/* 切换收藏：优先用列表里选中的那首，没选就收藏当前正在播的 */
void app_toggle_favorite(App *app)
{
    const wchar_t *path = NULL;
    int sel = -1;

    if (!app)
        return;

    if (app->hList)
        sel = ListView_GetNextItem(app->hList, -1, LVNI_SELECTED);

    if (sel >= 0) {
        Playlist *vpl = app_view_pl(app);
        if (sel < vpl->count)
            path = vpl->items[sel].path;
    }
    if (!path && app->current >= 0 && app->current < app->pl.count)
        path = app->pl.items[app->current].path;

    app_toggle_fav_path(app, path);
}
/* ================= 手动指定歌词文件 ================= */

void app_open_lyric_file(App *app)
{
    wchar_t file[MAX_PATH];
    OPENFILENAMEW ofn;
    const wchar_t *audio = NULL;

    if (!app)
        return;

    file[0] = 0;
    ZeroMemory(&ofn, sizeof(ofn));
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner   = app->hMain;
    ofn.lpstrFilter = L"歌词文件 (*.lrc)\0*.lrc\0文本文件 (*.txt)\0*.txt\0所有文件\0*.*\0";
    ofn.lpstrFile   = file;
    ofn.nMaxFile    = MAX_PATH;
    ofn.Flags       = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_EXPLORER;
    ofn.lpstrTitle  = L"选择歌词文件";
    ofn.lpstrDefExt = L"lrc";

    if (!GetOpenFileNameW(&ofn))
        return;

    if (app->current >= 0 && app->current < app->pl.count)
        audio = app->pl.items[app->current].path;
    if (!audio)
        return;

    /* 记住这首歌配的歌词（按文件名记，歌曲换目录也还能对上） */
    {
        wchar_t key[160];
        swprintf(key, 160, L"lrc_%ls", util_basename(audio));
        store_set_str(key, file);
    }

    /* lrc_load_for 的逻辑是"把扩展名换成 .lrc"；
       传进去的本来就是 .lrc，替换后原样不变，所以能直接按路径加载。 */
    lrc_free(&app->lrc);
    app->lrcValid = 0;
    if (lrc_load_for(&app->lrc, file) > 0)
        app->lrcValid = 1;

    if (app->hLyric)
        lyricview_set_lrc(app->hLyric, app->lrcValid ? &app->lrc : NULL,
                          util_basename(audio));
    ui_update_lyric(app, 1);
    app_np_refresh(app);
}
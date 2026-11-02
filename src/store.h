/* store.h —— 应用数据的持久化（配置 / 曲库 / 播放列表 / 最近播放）
 *
 * 所有数据放在 exe 同目录的 data\ 下，便携、便于查看和备份：
 *   data\config.ini       音量、播放模式、上次视图、窗口位置、扫描目录
 *   data\library.txt      我的歌曲（一行一个路径）
 *   data\recent.txt       最近播放（次数|时间戳|路径）
 *   data\playlists\*.m3u   用户播放列表（标准 m3u 格式，别的播放器也能读）
 */
#ifndef STORE_H
#define STORE_H

#include <windows.h>
#include "playlist.h"

#define STORE_NAME_MAX 128

typedef struct RecentEntry {
    wchar_t   path[TRACK_PATH_MAX];
    long long lastPlayed;   /* Unix 时间戳 */
    int       count;        /* 播放次数 */
} RecentEntry;

/* --- 初始化：确定 data\ 目录（不存在则创建）。返回 1 成功 --- */
int  store_init(void);
const wchar_t *store_dir(void);

/* --- 配置（字符串键值）--- */
void store_load_config(void);
void store_save_config(void);
int  store_get_int(const wchar_t *key, int def);
void store_set_int(const wchar_t *key, int val);
/* 取字符串；返回的指针在下一次取值前有效 */
const wchar_t *store_get_str(const wchar_t *key);
void store_set_str(const wchar_t *key, const wchar_t *val);

/* --- 我的歌曲 --- */
int  store_load_library(Playlist *pl);
int  store_save_library(const Playlist *pl);

/* --- 最近播放 ---
   返回记录条数；pl 会被填成按"最近播放时间倒序"排列的曲目，
   entries 可选，用来取播放次数（可为 NULL）。调用方负责 free(*entries)。 */
int  store_load_favorites(Playlist *pl);
int  store_save_favorites(const Playlist *pl);
void store_touch_favorite(const wchar_t *path, int on);

int  store_load_recent(Playlist *pl, RecentEntry **entries);
int  store_save_recent(const Playlist *pl, const RecentEntry *entries, int n);
/* 记一次播放：把 path 提到最前面，次数 +1。返回 1 表示曲目信息有变化 */
int  store_touch_recent(Playlist *pl, RecentEntry **entries, int *n,
                        const wchar_t *path);

/* --- 播放列表 --- */
int  store_list_playlists(wchar_t names[][STORE_NAME_MAX], int maxNames);
int  store_load_playlist(const wchar_t *name, Playlist *pl);
int  store_save_playlist(const wchar_t *name, const Playlist *pl);
int  store_delete_playlist(const wchar_t *name);

#endif /* STORE_H */

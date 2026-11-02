/* playlist.h —— 曲目与播放列表 */
#ifndef PLAYLIST_H
#define PLAYLIST_H

#include <windows.h>

#define TRACK_PATH_MAX  1024
#define TRACK_TITLE_MAX 256
#define TRACK_META_MAX  128

typedef struct Track {
    wchar_t path[TRACK_PATH_MAX];
    wchar_t title[TRACK_TITLE_MAX];   /* Alpha 暂用文件名；v0.2 换成 ID3 标题 */
    wchar_t artist[TRACK_META_MAX];
    wchar_t album[TRACK_META_MAX];
    int     length_ms;                /* -1 = 未知 */
} Track;

typedef struct Playlist {
    Track *items;
    int    count;
    int    cap;
} Playlist;

void playlist_init(Playlist *pl);
void playlist_free(Playlist *pl);
void playlist_clear(Playlist *pl);

/* 追加一首；重复（同路径，忽略大小写）或失败返回 -1，成功返回新索引 */
int  playlist_add(Playlist *pl, const wchar_t *path);

int  playlist_find(const Playlist *pl, const wchar_t *path);
int  playlist_remove(Playlist *pl, int idx);

/* 整条复制一个曲目（连 ID3 信息一起），用于构建过滤列表和播放队列。
   用 playlist_add 的话会重新读一遍 ID3，过滤/搜索时太慢。 */
int  playlist_append_track(Playlist *pl, const Track *t);

/* 递归（或单层）扫描目录里的音频文件并加入列表；返回新增数量 */
int  library_scan_dir(Playlist *pl, const wchar_t *dir, int recursive);

/* 把列表里的路径复制一份（保持顺序），用于"当前视图 -> 播放队列" */
int  playlist_copy_paths(Playlist *dst, const Playlist *src);

/* m3u 读写（标准 #EXTM3U 格式，别的播放器也能读） */
int  playlist_save_m3u(const wchar_t *file, const Playlist *pl);
int  playlist_load_m3u(const wchar_t *file, Playlist *pl);

#endif /* PLAYLIST_H */

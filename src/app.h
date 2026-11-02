/* app.h —— 应用状态、控件 ID、模块间接口 */
#ifndef APP_H
#define APP_H

#include <windows.h>
#include <commctrl.h>

#include "audio.h"
#include "playlist.h"
#include "store.h"
#include "lrc.h"

/* ---- 应用标识 ---- */
#define APP_NAME_TEXT    L"重庆大工程music"
#define APP_APP_SUB      L"Chongqing Polytechnic University of Engineering"
#define APP_VERSION_TEXT L"Beta v1.0"
#define APP_BUILD_TAG    L"build 2026-10-01-1910-wic2"

/* ---- 资源 ID（与 res/resource.h 共用） ---- */
#include "../res/resource.h"

#define IDR_MAINMENU   100

/* ---- 菜单命令：文件 ---- */
#define IDM_FILE_OPEN_FILES   40001
#define IDM_FILE_ADD_FOLDER   40002
#define IDM_FILE_SCAN_MUSIC   40003
#define IDM_FILE_EXIT         40004
#define IDM_EDIT_REMOVE       40005
#define IDM_FILE_CLEAR_LIB    40006

/* ---- 菜单命令：播放 ---- */
#define IDM_PLAY_TOGGLE       40101
#define IDM_PLAY_STOP         40102
#define IDM_PLAY_NEXT         40103
#define IDM_PLAY_PREV         40104
#define IDM_PLAY_ALL          40105

#define IDM_MODE_SEQ          40110
#define IDM_MODE_LOOP_ALL     40111
#define IDM_MODE_LOOP_ONE     40112
#define IDM_MODE_SHUFFLE      40113

/* ---- 菜单命令：播放列表 ---- */
#define IDM_LIST_NEW          40201
#define IDM_LIST_ADD_CURRENT  40202
#define IDM_LIST_REMOVE       40203
#define IDM_LIST_RENAME       40204
#define IDM_LIST_DELETE       40205
#define IDM_LIST_SAVE_AS      40206
#define IDM_LIST_LOAD_FILE    40207

/* ---- 菜单命令：视图 ---- */
#define IDM_VIEW_LIBRARY      40301
#define IDM_VIEW_PLAYLISTS    40302
#define IDM_VIEW_RECENT       40303
#define IDM_VIEW_LYRIC        40304
#define IDM_VIEW_REFRESH      40305
#define IDM_VIEW_CLEAR_RECENT 40306
#define IDM_VIEW_ARTISTS      40307
#define IDM_VIEW_ALBUMS       40308
#define IDM_VIEW_FAVORITES    40309
#define IDM_VIEW_NOWPLAYING   40310
#define IDM_FAV_TOGGLE        40311
#define IDM_LYRIC_OPEN        40312

#define IDM_HELP_ABOUT        40401

/* ---- 控件 ID ---- */
#define IDC_LIST        41001
#define IDC_PROGRESS    41002
#define IDC_VOLUME      41003
#define IDC_STATUS      41004
#define IDC_TIMECUR     41005
#define IDC_TIMETOTAL   41006

/* 工具栏按钮（顺序即索引） */
enum {
    BTN_OPENF = 0, BTN_ADDFOLDER, BTN_PREV, BTN_PLAY, BTN_STOP, BTN_NEXT,
    BTN_MODE, BTN_LYRIC, BTN_COUNT
};
#define IDC_BTN_BASE    41010
#define IDC_BTN_OPENF   (IDC_BTN_BASE + BTN_OPENF)
#define IDC_BTN_ADDFOLD (IDC_BTN_BASE + BTN_ADDFOLDER)
#define IDC_BTN_PREV    (IDC_BTN_BASE + BTN_PREV)
#define IDC_BTN_PLAY    (IDC_BTN_BASE + BTN_PLAY)
#define IDC_BTN_STOP    (IDC_BTN_BASE + BTN_STOP)
#define IDC_BTN_NEXT    (IDC_BTN_BASE + BTN_NEXT)
#define IDC_BTN_MODE    (IDC_BTN_BASE + BTN_MODE)
#define IDC_BTN_LYRIC   (IDC_BTN_BASE + BTN_LYRIC)

/* 视图区：左侧导航 + 中间列表 + 右侧歌词 */
#define IDC_NAV         41030
#define IDC_LYRIC       41031
#define IDC_VIEWTITLE   41032
#define IDC_SEARCH      41033

/* 视图头部操作按钮（标签随视图变化） */
#define IDC_HDR1        41040
#define IDC_HDR2        41041
#define IDC_HDR3        41042
#define IDC_HDR4        41043
#define HDR_COUNT       4

#define IDT_TICK        42001

typedef enum {
    MODE_SEQUENTIAL = 0,
    MODE_LOOP_ALL,
    MODE_LOOP_ONE,
    MODE_SHUFFLE
} PlayMode;

/* ---- 视图 ---- */
typedef enum {
    VIEW_LIBRARY = 0,   /* 我的歌曲 */
    VIEW_ARTISTS,       /* 按歌手分组 */
    VIEW_ALBUMS,        /* 按专辑分组 */
    VIEW_PLAYLISTS,     /* 播放列表总览 */
    VIEW_PLAYLIST,      /* 某个具体播放列表 */
    VIEW_RECENT,        /* 最近播放 */
    VIEW_FAVORITES,     /* 我喜欢的音乐 */
    VIEW_NOWPLAYING     /* 正在播放 */
} ViewId;
#define VIEW_COUNT 8

/* 分组（歌手 / 专辑） */
#define MAX_GROUPS   512
#define GROUP_NAME_MAX 160

typedef struct Group {
    wchar_t name[GROUP_NAME_MAX];
    int     count;
} Group;

/* 过滤来源：分组下钻 or 搜索 */
typedef enum { FILTER_NONE = 0, FILTER_ARTIST, FILTER_ALBUM } FilterKind;

#define MAX_USER_LISTS 32

typedef struct UserList {
    wchar_t  name[STORE_NAME_MAX];
    Playlist pl;
} UserList;

typedef struct App {
    HINSTANCE hInst;
    HWND  hMain;
    HWND  hList;
    HWND  hNav;
    HWND  hLyric;
    HWND  hViewTitle;
    HWND  hHdr[HDR_COUNT];
    HWND  hProgress;
    HWND  hVolume;
    HWND  hStatus;
    HWND  hTimeCur;
    HWND  hTimeTotal;
    HWND  hBtn[BTN_COUNT];
    HFONT hFont;
    HFONT hFontBold;

    Audio   *audio;

    /* 当前播放队列（app_play_index / next / prev 都基于它） */
    Playlist pl;

    /* 曲库与历史 */
    Playlist     library;      /* 我的歌曲 */
    Playlist     recent;       /* 最近播放（顺序 = 最近在前） */
    Playlist     favorites;    /* 我喜欢的音乐 */
    RecentEntry *recentMeta;
    int          recentCount;

    /* 用户播放列表 */
    UserList lists[MAX_USER_LISTS];
    int      listCount;

    /* 视图状态 */
    ViewId   view;
    int      curList;          /* view == VIEW_PLAYLIST 时的下标 */
    int      showLyric;

    /* 分组（歌手 / 专辑） */
    Group    groups[MAX_GROUPS];
    int      groupCount;

    /* 过滤：分组下钻 + 搜索 */
    FilterKind filterKind;
    wchar_t    filterText[GROUP_NAME_MAX];
    wchar_t    searchText[GROUP_NAME_MAX];
    Playlist   filtered;       /* 过滤后的显示列表 */
    int        filterActive;
    HWND       hSearch;
    HWND       hNowPlaying;
    HWND       hTopBar;      /* 工具栏底色条（子窗口，可靠绘制） */
    HWND       hBtnFav;

    /* 封面网格（歌手 / 专辑视图） */
    HIMAGELIST hCovers;
    int        coverSize;      /* 缩略图边长（像素） */
    int        coverDirty;     /* 需要重建封面 */
    int        listIsIcon;     /* 当前列表是不是卡片模式 */

    /* 歌词 */
    Lrc      lrc;
    int      lrcValid;

    /* 播放状态 */
    int      current;          /* 当前曲目在 pl 里的下标，-1 表示没有 */
    PlayMode mode;
    int      volume;
    int      dragging;
    int      lastRandom;
    unsigned rng;
    int      dpi;
    int      shownPlaying;
} App;

extern App g_app;
extern const wchar_t *const g_mode_names[4];

int ui_scale(App *app, int px);

/* ---- 数据与逻辑（app.c） ---- */
void app_init(App *app, HINSTANCE hInst, HWND hMain);
void app_free(App *app);

Playlist *app_view_pl(App *app);
const wchar_t *app_view_name(App *app);

void app_set_view(App *app, ViewId v, int listIdx);
void app_rebuild_nav(App *app);
void app_refresh_view(App *app);

/* 分组与过滤 */
void app_build_groups(App *app, int byAlbum);
void app_open_group(App *app, int index);      /* 下钻到某个歌手/专辑 */
void app_clear_filter(App *app);               /* 返回上级（清掉分组过滤） */
void app_set_search(App *app, const wchar_t *text);
void app_apply_filter(App *app);

int  app_play_index(App *app, int idx);
int  app_play_from_view(App *app, int row);
void app_toggle_play(App *app);
void app_stop(App *app);
void app_next(App *app, int userInit);
void app_prev(App *app);
void app_set_mode(App *app, PlayMode m);
void app_set_volume(App *app, int vol);
void app_on_tick(App *app);

void app_open_files(App *app);
void app_add_folder(App *app);
void app_scan_music_folder(App *app);
int  app_open_paths(App *app, const wchar_t *cmdline);

void app_remove_selected(App *app);
void app_clear_library(App *app);
void app_play_all_visible(App *app);

void app_new_playlist(App *app);
void app_add_selected_to_playlist(App *app);
void app_delete_current_playlist(App *app);
void app_rename_current_playlist(App *app);
void app_save_current_playlist(App *app);
void app_load_playlist_file(App *app);
void app_clear_recent(App *app);

void app_load_lyric_for_current(App *app);
void app_open_lyric_file(App *app);   /* 手动给当前歌曲指定 .lrc */

/* 左侧导航 / 歌词面板的点击回调（由主窗口转发） */
void app_on_nav_click(App *app, int index);
void app_on_lyric_click(App *app, int line);

int  app_input_box(HWND owner, const wchar_t *title, const wchar_t *prompt,
                   wchar_t *buf, int cch);

/* ---- 界面（ui.c） ---- */
void ui_create(App *app);
void ui_layout(App *app);
void ui_refresh_list(App *app);
int  ui_find_row_of_path(App *app, const wchar_t *path);
void ui_update_row(App *app, int idx);
void ui_sync_selection(App *app);
void ui_update_progress(App *app);
void ui_update_times(App *app, int cur, int total);
void ui_update_play_button(App *app);
void ui_update_mode_button(App *app);
void ui_update_status(App *app, const wchar_t *extra);
void ui_update_lyric(App *app, int force);
void ui_scroll_to_playing(App *app);
void ui_build_covers(App *app);

/* 播放状态与收藏 */
int  app_is_playing(App *app);
int  app_current_position(App *app);
int  app_current_length(App *app);
int  app_is_favorite(App *app, const wchar_t *path);
void app_toggle_favorite(App *app);
void app_toggle_fav_path(App *app, const wchar_t *path);
void app_np_refresh(App *app);
int  ui_toolbar_height(App *app);
int  ui_on_command(App *app, int id);
void ui_on_list_dblclk(App *app, int row);
void ui_on_hscroll(App *app, HWND hCtl, int code);

/* ---- 音频后端自检（selftest.c） ---- */
int selftest_run(HINSTANCE hInst, int quiet);

#endif /* APP_H */

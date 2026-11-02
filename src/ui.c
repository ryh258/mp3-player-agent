/* ui.c —— 界面：左侧导航 + 中间列表 + 右侧歌词 + 底部播放控制条 */
#include "app.h"
#include "util.h"
#include "nav.h"
#include "lyricview.h"
#include "cover.h"
#include "tag_id3.h"
#include "theme.h"
#include "nowplaying.h"

#include <commctrl.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <wchar.h>

/* ---- 设计尺寸（96 DPI 基准，运行时按 DPI 缩放）---- */
#define TB_H       46      /* 顶部工具栏 */
#define HDR_H      38      /* 视图标题 + 操作按钮行 */
#define ROW_H      40      /* 底部进度行 */
#define STATUS_H   24
#define PAD        8
#define NAV_W      196     /* 左侧导航宽 */
#define LYRIC_W    268     /* 右侧歌词宽 */
#define VOL_W      140

#define TB_HEIGHT_PX 34    /* 工具栏按钮高度 */

/* 工具栏按钮文字与宽度 */
static const wchar_t *const g_btn_text[BTN_COUNT] = {
    L"打开文件", L"添加文件夹", L"上一首", L"播放", L"停止", L"下一首",
    L"顺序播放", L"歌词"
};
static const int g_btn_width[BTN_COUNT] = {
    84, 100, 72, 72, 72, 72, 104, 72
};

/* 每个视图的头部按钮：[4] = {文字, 命令}，文字为 NULL 表示隐藏 */
typedef struct HdrDef { const wchar_t *label; int cmd; } HdrDef;

static const HdrDef g_hdr[VIEW_COUNT][HDR_COUNT] = {
    /* 0 我的歌曲 */
    { { L"播放全部", IDM_PLAY_ALL },
      { L"添加到播放列表", IDM_LIST_ADD_CURRENT },
      { L"从曲库移除", IDM_EDIT_REMOVE },
      { L"清空曲库", IDM_FILE_CLEAR_LIB } },
    /* 1 歌手 */
    { { L"刷新", IDM_VIEW_REFRESH },
      { NULL, 0 }, { NULL, 0 }, { NULL, 0 } },
    /* 2 专辑 */
    { { L"刷新", IDM_VIEW_REFRESH },
      { NULL, 0 }, { NULL, 0 }, { NULL, 0 } },
    /* 3 播放列表总览 */
    { { L"新建播放列表", IDM_LIST_NEW },
      { L"导入 m3u", IDM_LIST_LOAD_FILE },
      { NULL, 0 }, { NULL, 0 } },
    /* 4 某个播放列表 */
    { { L"播放全部", IDM_PLAY_ALL },
      { L"添加歌曲", IDM_LIST_ADD_CURRENT },
      { L"重命名", IDM_LIST_RENAME },
      { L"删除列表", IDM_LIST_DELETE } },
    /* 5 最近播放 */
    { { L"播放全部", IDM_PLAY_ALL },
      { L"添加到播放列表", IDM_LIST_ADD_CURRENT },
      { L"清空记录", IDM_VIEW_CLEAR_RECENT },
      { NULL, 0 } },
    /* 6 我喜欢的音乐 */
    { { L"播放全部", IDM_PLAY_ALL },
      { L"添加到播放列表", IDM_LIST_ADD_CURRENT },
      { L"取消收藏", IDM_FAV_TOGGLE },
      { NULL, 0 } },
    /* 7 正在播放 */
    { { NULL, 0 }, { NULL, 0 }, { NULL, 0 }, { NULL, 0 } }
};

int ui_scale(App *app, int px)
{
    int dpi = (app && app->dpi) ? app->dpi : 96;
    return MulDiv(px, dpi, 96);
}

/* ================= 字体 ================= */

static HFONT make_msg_font(App *app, int bold)
{
    NONCLIENTMETRICSW ncm;
    LOGFONTW lf;

    ZeroMemory(&ncm, sizeof(ncm));
    ncm.cbSize = sizeof(ncm);
    if (SystemParametersInfoW(SPI_GETNONCLIENTMETRICS, sizeof(ncm), &ncm, 0)) {
        lf = ncm.lfMessageFont;
    } else {
        ZeroMemory(&lf, sizeof(lf));
        wcscpy(lf.lfFaceName, L"Microsoft YaHei UI");
    }
    lf.lfHeight = -MulDiv(9, (app && app->dpi) ? app->dpi : 96, 72);
    lf.lfWeight = bold ? FW_BOLD : FW_NORMAL;
    return CreateFontIndirectW(&lf);
}

/* ================= 列表列 ================= */

/* 每个视图用同一组 5 列，只是表头和宽度不同 —— 不用的列宽度设 0 隐藏 */
/* 顶层工具栏的高度（主窗口画背景时要用） */
int ui_toolbar_height(App *app)
{
    return ui_scale(app, 52);
}

/* ---- 工具栏底色条 ----
   主窗口自己的 WM_ERASEBKGND 和类背景刷在这台机器上都不生效
   （实测过，那条始终是浅灰），所以干脆用一个子窗口专门画这块底色，
   再把它压到 z 序最底，按钮就浮在它上面。 */
static LRESULT CALLBACK TopBarProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    (void)wp; (void)lp;
    switch (msg) {
    case WM_ERASEBKGND:
        return 1;
    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC dc = BeginPaint(hwnd, &ps);
        RECT rc;
        GetClientRect(hwnd, &rc);
        theme_fill_vgrad(dc, &rc, TH_TOOLBAR, TH_TOOLBAR);
        EndPaint(hwnd, &ps);
        return 0;
    }
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

static void register_topbar(HINSTANCE hi)
{
    WNDCLASSEXW wc;
    ZeroMemory(&wc, sizeof(wc));
    wc.cbSize = sizeof(wc);
    wc.style = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = TopBarProc;
    wc.hInstance = hi;
    wc.hCursor = LoadCursorW(NULL, IDC_ARROW);
    wc.hbrBackground = NULL;
    wc.lpszClassName = L"Mp3TopBarWnd";
    RegisterClassExW(&wc);
}
static void setup_columns(App *app)
{
    static const wchar_t *hdrs[VIEW_COUNT][5] = {
        { L"序号", L"标题", L"歌手",   L"专辑",     L"时长" },      /* 0 我的歌曲 */
        { L"序号", L"歌手", L"歌曲数", NULL,        NULL },          /* 1 歌手 */
        { L"序号", L"专辑", L"歌曲数", NULL,        NULL },          /* 2 专辑 */
        { L"序号", L"名称", L"曲目数", NULL,        NULL },          /* 3 播放列表总览 */
        { L"序号", L"标题", L"歌手",   L"专辑",     L"时长" },      /* 4 某个播放列表 */
        { L"序号", L"标题", L"歌手",   L"播放次数", L"最后播放" },   /* 5 最近播放 */
        { L"序号", L"标题", L"歌手",   L"专辑",     L"时长" },       /* 6 我喜欢的音乐 */
        { L"序号", L"标题", L"歌手",   L"专辑",     L"时长" }        /* 7 正在播放 */
    };
    int i;

    while (SendMessageW(app->hList, LVM_DELETECOLUMN, 0, 0))
        ;

    for (i = 0; i < 5; i++) {
        LVCOLUMNW c;
        ZeroMemory(&c, sizeof(c));
        c.mask = LVCF_TEXT | LVCF_WIDTH | LVCF_SUBITEM;
        c.cx = 100;
        c.pszText = (LPWSTR)(hdrs[app->view][i] ? hdrs[app->view][i] : L"");
        c.iSubItem = i;
        SendMessageW(app->hList, LVM_INSERTCOLUMNW, (WPARAM)i, (LPARAM)&c);
    }
}

/* ================= 创建 ================= */

void ui_create(App *app)
{
    int i;

    app->hFont = make_msg_font(app, 0);
    app->hFontBold = make_msg_font(app, 1);

    /* --- 工具栏底色条：必须最先创建，先建的子窗口天然在 z 序最底，
           按钮之后创建就浮在它上面。 --- */
    register_topbar(app->hInst);
    app->hTopBar = CreateWindowExW(0, L"Mp3TopBarWnd", L"", WS_CHILD | WS_VISIBLE,
                                   0, 0, 10, 10, app->hMain, NULL, app->hInst, NULL);

    /* --- 工具栏按钮 --- */
    for (i = 0; i < BTN_COUNT; i++) {
        app->hBtn[i] = CreateWindowExW(0, L"BUTTON", g_btn_text[i],
            WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON,
            0, 0, 10, 10, app->hMain,
            (HMENU)(INT_PTR)(IDC_BTN_BASE + i), app->hInst, NULL);
        SendMessageW(app->hBtn[i], WM_SETFONT, (WPARAM)app->hFont, TRUE);
    }

    /* --- 搜索框 --- */
    app->hSearch = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"",
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL,
        0, 0, 10, 10, app->hMain, (HMENU)IDC_SEARCH, app->hInst, NULL);
    SendMessageW(app->hSearch, WM_SETFONT, (WPARAM)app->hFont, TRUE);
    SendMessageW(app->hSearch, EM_SETCUEBANNER, TRUE,
                 (LPARAM)L"搜索歌曲 / 歌手 / 专辑");


    /* --- 正在播放页（自绘黑胶唱片，默认隐藏） --- */
    app->hNowPlaying = CreateWindowExW(0, NOWPLAYING_CLASS, L"",
        WS_CHILD | WS_CLIPSIBLINGS,
        0, 0, 10, 10, app->hMain, NULL, app->hInst, NULL);
    if (app->hNowPlaying)
        nowplaying_set_app(app->hNowPlaying, app);

    /* --- 左侧导航 --- */
    app->hNav = CreateWindowExW(0, NAV_CLASS, L"",
        WS_CHILD | WS_VISIBLE,
        0, 0, 10, 10, app->hMain,
        (HMENU)IDC_NAV, app->hInst, NULL);
    nav_set_dpi(app->hNav, app->dpi);
    nav_set_font(app->hNav, app->hFont);

    /* --- 右侧歌词 --- */
    app->hLyric = CreateWindowExW(0, LYRICVIEW_CLASS, L"",
        WS_CHILD | (app->showLyric ? WS_VISIBLE : 0),
        0, 0, 10, 10, app->hMain,
        (HMENU)IDC_LYRIC, app->hInst, NULL);
    lyricview_set_dpi(app->hLyric, app->dpi);
    lyricview_set_font(app->hLyric, app->hFont);

    /* --- 视图标题 --- */
    app->hViewTitle = CreateWindowExW(0, L"STATIC", L"",
        WS_CHILD | WS_VISIBLE | SS_LEFT | SS_CENTERIMAGE,
        0, 0, 10, 10, app->hMain, (HMENU)IDC_VIEWTITLE, app->hInst, NULL);
    SendMessageW(app->hViewTitle, WM_SETFONT, (WPARAM)app->hFontBold, TRUE);

    /* --- 视图头部操作按钮 --- */
    for (i = 0; i < HDR_COUNT; i++) {
        app->hHdr[i] = CreateWindowExW(0, L"BUTTON", L"",
            WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON,
            0, 0, 10, 10, app->hMain,
            (HMENU)(INT_PTR)(IDC_HDR1 + i), app->hInst, NULL);
        SendMessageW(app->hHdr[i], WM_SETFONT, (WPARAM)app->hFont, TRUE);
    }

    /* --- 主列表 --- */
    app->hList = CreateWindowExW(WS_EX_CLIENTEDGE, WC_LISTVIEWW, L"",
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | LVS_REPORT | LVS_SHOWSELALWAYS,
        0, 0, 10, 10, app->hMain, (HMENU)IDC_LIST, app->hInst, NULL);
    SendMessageW(app->hList, WM_SETFONT, (WPARAM)app->hFont, TRUE);
    SendMessageW(app->hList, LVM_SETEXTENDEDLISTVIEWSTYLE, 0,
        (LPARAM)(LVS_EX_FULLROWSELECT | LVS_EX_GRIDLINES | LVS_EX_DOUBLEBUFFER |
                 LVS_EX_HEADERDRAGDROP));
    setup_columns(app);

    /* --- 底部时间 / 进度 --- */
    app->hTimeCur = CreateWindowExW(0, L"STATIC", L"--:--",
        WS_CHILD | WS_VISIBLE | SS_CENTER | SS_CENTERIMAGE,
        0, 0, 10, 10, app->hMain, (HMENU)IDC_TIMECUR, app->hInst, NULL);
    SendMessageW(app->hTimeCur, WM_SETFONT, (WPARAM)app->hFont, TRUE);

    app->hTimeTotal = CreateWindowExW(0, L"STATIC", L"--:--",
        WS_CHILD | WS_VISIBLE | SS_CENTER | SS_CENTERIMAGE,
        0, 0, 10, 10, app->hMain, (HMENU)IDC_TIMETOTAL, app->hInst, NULL);
    SendMessageW(app->hTimeTotal, WM_SETFONT, (WPARAM)app->hFont, TRUE);

    app->hProgress = CreateWindowExW(0, TRACKBAR_CLASSW, L"",
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | TBS_HORZ | TBS_NOTICKS,
        0, 0, 10, 10, app->hMain, (HMENU)IDC_PROGRESS, app->hInst, NULL);
    SendMessageW(app->hProgress, TBM_SETRANGE, FALSE, MAKELPARAM(0, 1000));
    SendMessageW(app->hProgress, TBM_SETPOS, TRUE, 0);

    /* --- 音量 --- */
    app->hVolume = CreateWindowExW(0, TRACKBAR_CLASSW, L"",
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | TBS_HORZ | TBS_NOTICKS,
        0, 0, 10, 10, app->hMain, (HMENU)IDC_VOLUME, app->hInst, NULL);
    SendMessageW(app->hVolume, TBM_SETRANGE, FALSE, MAKELPARAM(0, 100));
    SendMessageW(app->hVolume, TBM_SETPOS, TRUE, app->volume);

    /* --- 状态栏 --- */
    app->hStatus = CreateWindowExW(0, STATUSCLASSNAMEW, L"",
        WS_CHILD | WS_VISIBLE | SBARS_SIZEGRIP,
        0, 0, 10, 10, app->hMain, (HMENU)IDC_STATUS, app->hInst, NULL);
    SendMessageW(app->hStatus, WM_SETFONT, (WPARAM)app->hFont, TRUE);
}

/* ================= 布局 ================= */

void ui_layout(App *app)
{
    RECT rc;
    int W, H;
    int pad    = ui_scale(app, PAD);
    int tbH    = ui_scale(app, TB_H);
    int hdrH   = ui_scale(app, HDR_H);
    int rowH   = ui_scale(app, ROW_H);
    int stH    = ui_scale(app, STATUS_H);
    int navW   = ui_scale(app, NAV_W);
    int lyW    = app->showLyric ? ui_scale(app, LYRIC_W) : 0;
    int volW   = ui_scale(app, VOL_W);
    int timeW  = ui_scale(app, 56);
    int btnH   = ui_scale(app, TB_HEIGHT_PX);
    int x, y, i;

    GetClientRect(app->hMain, &rc);
    W = rc.right;
    H = rc.bottom;

    /* --- 顶部工具栏 --- */
    y = pad;
    x = pad;
    for (i = 0; i < BTN_COUNT; i++) {
        int bw;
        if (i == BTN_PREV || i == BTN_MODE)
            x += ui_scale(app, 16);
        bw = ui_scale(app, g_btn_width[i]);
        if (i == BTN_LYRIC) {
            /* 歌词开关按钮贴右边，音量条左边 */
            continue;
        }
        if (i == BTN_MODE) {
            MoveWindow(app->hBtn[i], x, y, bw, btnH, TRUE);
            x += bw + ui_scale(app, 4);
            continue;
        }
        MoveWindow(app->hBtn[i], x, y, bw, btnH, TRUE);
        x += bw + ui_scale(app, 4);
    }
    {
        int lyBtnW = ui_scale(app, g_btn_width[BTN_LYRIC]);
        int lyBtnX = W - pad - lyBtnW;
        int volX = lyBtnX - ui_scale(app, 10) - volW;
        int srchW = ui_scale(app, 190);
        int srchX = volX - ui_scale(app, 14) - srchW;
        MoveWindow(app->hBtn[BTN_LYRIC], lyBtnX, y, lyBtnW, btnH, TRUE);
        MoveWindow(app->hVolume, volX, y + ui_scale(app, 6), volW, ui_scale(app, 22), TRUE);
        MoveWindow(app->hSearch, srchX, y + ui_scale(app, 6), srchW, ui_scale(app, 22), TRUE);
    }
    /* 播放/暂停按钮加粗突出 */
    SendMessageW(app->hBtn[BTN_PLAY], BM_SETSTYLE, BS_DEFPUSHBUTTON, TRUE);

    /* --- 状态栏贴底 --- */
    MoveWindow(app->hStatus, 0, H - stH, W, stH, TRUE);

    /* --- 底部进度行 --- */
    {
        int rowY = H - stH - rowH;
        MoveWindow(app->hTimeCur,   pad,             rowY, timeW, rowH, TRUE);
        MoveWindow(app->hTimeTotal, W - pad - timeW, rowY, timeW, rowH, TRUE);
        MoveWindow(app->hProgress,  pad + timeW + ui_scale(app, 6),
                    rowY + ui_scale(app, 8),
                    W - 2 * (pad + timeW + ui_scale(app, 6)),
                    ui_scale(app, 24), TRUE);
    }

    /* --- 导航 / 列表 / 歌词 --- */
    {
        int top = tbH;
        int bottom = H - stH - rowH;
        int bodyH = bottom - top;
        int midX = navW;
        int midW = W - navW - lyW;

        if (bodyH < ui_scale(app, 60)) bodyH = ui_scale(app, 60);
        if (midW < ui_scale(app, 200)) midW = ui_scale(app, 200);

        /* 工具栏底色条：铺满顶部（不改 z 序，靠创建顺序保证在最底） */
    if (app->hTopBar)
        MoveWindow(app->hTopBar, 0, 0, W, tbH, TRUE);

    MoveWindow(app->hNav, 0, top, navW, bodyH, TRUE);

        /* 视图标题 + 头部按钮 */
        MoveWindow(app->hViewTitle, midX + pad, top + ui_scale(app, 4),
                   midW - 2 * pad, hdrH - ui_scale(app, 8), TRUE);
        {
            int bx = midX + midW - pad;
            for (i = HDR_COUNT - 1; i >= 0; i--) {
                int bw;
                const wchar_t *lbl = g_hdr[(int)app->view][i].label;
                if (!lbl) {
                    ShowWindow(app->hHdr[i], SW_HIDE);
                    continue;
                }
                bw = ui_scale(app, (int)(wcslen(lbl) * 13 + 22));
                bx -= bw;
                MoveWindow(app->hHdr[i], bx, top + ui_scale(app, 5), bw,
                           hdrH - ui_scale(app, 12), TRUE);
                bx -= ui_scale(app, 6);
            }
        }

        {
            int listY = top + hdrH;
            int listH = bottom - listY;
            if (listH < ui_scale(app, 40)) listH = ui_scale(app, 40);
            MoveWindow(app->hList, midX, listY, midW, listH, TRUE);

            /* 列宽按视图设置：{序号, 主列(flex), 3, 4, 5} */
            if (!app->listIsIcon) {
                static const int wLib[5]  = {  56,   0, 140, 170,  80 };
                static const int wPls[5]  = {  56,   0, 100,   0,   0 };
                static const int wRec[5]  = {  56,   0, 140, 110, 130 };
                static const int wGrp[5]  = {  56,   0, 110,   0,   0 };
                const int *cw;
                int other = 0, i2;
                int flexW;

                switch (app->view) {
                case VIEW_PLAYLISTS: cw = wPls; break;
                case VIEW_RECENT:    cw = wRec; break;
                case VIEW_ARTISTS:
                case VIEW_ALBUMS:    cw = wGrp; break;
                default:             cw = wLib; break;
                }

                for (i2 = 0; i2 < 5; i2++) {
                    if (i2 != 1) {
                        int w = ui_scale(app, cw[i2]);
                        SendMessageW(app->hList, LVM_SETCOLUMNWIDTH, i2, MAKELPARAM(w, 0));
                        other += w;
                    }
                }
                flexW = midW - 2 * GetSystemMetrics(SM_CXEDGE) - other - ui_scale(app, 12);
                if (flexW < ui_scale(app, 100)) flexW = ui_scale(app, 100);
                SendMessageW(app->hList, LVM_SETCOLUMNWIDTH, 1, MAKELPARAM(flexW, 0));
            }
        }

        if (app->showLyric)
            MoveWindow(app->hLyric, W - lyW, top, lyW, bodyH, TRUE);

    /* 正在播放页盖满整个内容区（含标题行） */
    if (app->hNowPlaying)
        MoveWindow(app->hNowPlaying, midX, top, midW, bodyH, TRUE);
    }
}

/* ================= 列表内容 ================= */

static void set_sub(App *app, int item, int sub, const wchar_t *text)
{
    LVITEMW it;
    ZeroMemory(&it, sizeof(it));
    it.mask = LVIF_TEXT;
    it.iItem = item;
    it.iSubItem = sub;
    it.pszText = (LPWSTR)text;
    SendMessageW(app->hList, LVM_SETITEMW, 0, (LPARAM)&it);
}

static void insert_row(App *app, int row, const wchar_t *c0, const wchar_t *c1,
                       const wchar_t *c2, const wchar_t *c3, const wchar_t *c4)
{
    LVITEMW it;
    ZeroMemory(&it, sizeof(it));
    it.mask = LVIF_TEXT;
    it.iItem = row;
    it.iSubItem = 0;
    it.pszText = (LPWSTR)c0;
    SendMessageW(app->hList, LVM_INSERTITEMW, 0, (LPARAM)&it);
    set_sub(app, row, 1, c1 ? c1 : L"");
    set_sub(app, row, 2, c2 ? c2 : L"");
    set_sub(app, row, 3, c3 ? c3 : L"");
    set_sub(app, row, 4, c4 ? c4 : L"");
}

/* 相对时间：刚刚 / n 分钟前 / n 小时前 / 昨天 / n 天前 / 日期 */
static void fmt_relative(long long t, wchar_t *out, size_t cch)
{
    time_t now = time(NULL);
    long long d = (long long)now - t;

    if (t <= 0) { wcsncpy(out, L"—", cch); out[cch - 1] = 0; return; }
    if (d < 60)              swprintf(out, cch, L"刚刚");
    else if (d < 3600)       swprintf(out, cch, L"%d 分钟前", (int)(d / 60));
    else if (d < 86400)      swprintf(out, cch, L"%d 小时前", (int)(d / 3600));
    else if (d < 86400 * 2)  swprintf(out, cch, L"昨天");
    else if (d < 86400 * 30) swprintf(out, cch, L"%d 天前", (int)(d / 86400));
    else {
        time_t tt = (time_t)t;
        struct tm *tmv = localtime(&tt);
        if (tmv)
            swprintf(out, cch, L"%04d-%02d-%02d",
                     tmv->tm_year + 1900, tmv->tm_mon + 1, tmv->tm_mday);
        else
            wcsncpy(out, L"—", cch);
    }
    out[cch - 1] = 0;
}

int ui_find_row_of_path(App *app, const wchar_t *path)
{
    Playlist *vpl;
    if (app->view == VIEW_PLAYLISTS)
        return -1;
    vpl = app_view_pl(app);
    return playlist_find(vpl, path);
}

/* ---- 封面网格 ---- */

/* 给每个分组做一张封面：优先用该分组里第一首歌的内嵌封面（ID3 APIC），
   没有就生成一张校徽风格的默认封面。 */
void ui_build_covers(App *app)
{
    int size = ui_scale(app, 104);
    int byAlbum = (app->view == VIEW_ALBUMS);
    int i;

    if (app->hCovers && app->coverSize != size) {
        ImageList_Destroy(app->hCovers);
        app->hCovers = NULL;
    }
    if (!app->hCovers) {
        app->hCovers = ImageList_Create(size, size, ILC_COLOR32, 8, 32);
        app->coverSize = size;
    }
    if (!app->hCovers)
        return;

    ImageList_RemoveAll(app->hCovers);

    {
        FILE *lf = _wfopen(L"cover_debug.log", L"w, ccs=UTF-8");
        if (lf) {
            fwprintf(lf, L"=== ui_build_covers ===\n");
            fwprintf(lf, L"view=%d byAlbum=%d size=%d groupCount=%d libCount=%d\n",
                     (int)app->view, byAlbum, size, app->groupCount, app->library.count);
            fwprintf(lf, L"hCovers=%p coverSize=%d dpi=%d\n",
                     (void *)app->hCovers, app->coverSize, app->dpi);
            fwprintf(lf, L"cover_startup()=%d\n", cover_startup());
            fclose(lf);
        }
    }

    for (i = 0; i < app->groupCount; i++) {
        HBITMAP hbm = NULL;
        int j;


        for (j = 0; j < app->library.count && !hbm; j++) {
            Track *t = &app->library.items[j];
            const wchar_t *key = byAlbum
                ? (t->album[0] ? t->album : L"未知")
                : (t->artist[0] ? t->artist : L"未知");

            if (_wcsicmp(key, app->groups[i].name) != 0)
                continue;

            {
                unsigned char *data = NULL;
                unsigned len = 0;
                {
                    int okc = id3_cover(t->path, &data, &len);
                    {
                        FILE *lf = _wfopen(L"cover_debug.log", L"a, ccs=UTF-8");
                        if (lf) {
                            fwprintf(lf, L"    路径=%ls\n    id3_cover=%d len=%u\n", t->path, okc, len);
                            fclose(lf);
                        }
                    }
                    if (okc && len > 32) {
                        hbm = cover_from_memory(data, (int)len, size);
                        {
                            FILE *lf = _wfopen(L"cover_debug.log", L"a, ccs=UTF-8");
                            if (lf) {
                                fwprintf(lf, L"    cover_from_memory=%ls\n", hbm ? L"成功" : L"失败");
                                fclose(lf);
                            }
                        }
                    }
                    free(data);
                }
            }
        }

        {
            FILE *lf = _wfopen(L"cover_debug.log", L"a, ccs=UTF-8");
            if (lf) {
                fwprintf(lf, L"[组%d] 名称=%ls  命中歌曲下标=%d  hbm=%ls\n",
                         i, app->groups[i].name, j - 1, hbm ? L"有真封面" : L"无-将用兜底图");
                fclose(lf);
            }
        }

        if (!hbm)
            hbm = cover_default(size, app->groups[i].name, 0);

        if (hbm) {
            ImageList_Add(app->hCovers, hbm, NULL);
            DeleteObject(hbm);
        } else {
            /* 兜底：塞一张纯色图，保证下标和分组一一对应 */
            HDC dc = GetDC(app->hMain);
            HDC mem = CreateCompatibleDC(dc);
            HBITMAP blank = CreateCompatibleBitmap(dc, size, size);
            HGDIOBJ old = SelectObject(mem, blank);
            RECT r = { 0, 0, size, size };
            HBRUSH br = CreateSolidBrush(TH_BLUE);
            FillRect(mem, &r, br);
            DeleteObject(br);
            SelectObject(mem, old);
            DeleteDC(mem);
            ReleaseDC(app->hMain, dc);
            ImageList_Add(app->hCovers, blank, NULL);
            DeleteObject(blank);
        }
    }
    app->coverDirty = 0;
}

/* 歌手/专辑用卡片网格（LVS_ICON），其余用详细列表（LVS_REPORT） */
static void apply_list_mode(App *app)
{
    int wantIcon = (app->view == VIEW_ARTISTS || app->view == VIEW_ALBUMS);
    LONG style = GetWindowLongW(app->hList, GWL_STYLE);

    if (wantIcon) {
        int sp = app->coverSize + ui_scale(app, 34);
        style = (style & ~LVS_TYPEMASK) | LVS_ICON;
        SetWindowLongW(app->hList, GWL_STYLE, style);
        ListView_SetImageList(app->hList, app->hCovers, LVSIL_NORMAL);
        SendMessageW(app->hList, LVM_SETICONSPACING, 0,
                     MAKELPARAM(sp, app->coverSize + ui_scale(app, 54)));
        app->listIsIcon = 1;
    } else {
        if (app->listIsIcon) {
            style = (style & ~LVS_TYPEMASK) | LVS_REPORT;
            SetWindowLongW(app->hList, GWL_STYLE, style);
            ListView_SetImageList(app->hList, NULL, LVSIL_NORMAL);
            app->listIsIcon = 0;
        }
        setup_columns(app);
    }
}

static void refresh_view_header(App *app)
{
    wchar_t title[512];
    int count;

    if (app->view == VIEW_PLAYLISTS)
        count = app->listCount;
    else if (app->view == VIEW_ARTISTS || app->view == VIEW_ALBUMS)
        count = app->groupCount;
    else
        count = app_view_pl(app)->count;

    if (app->view == VIEW_ARTISTS)
        swprintf(title, 512, L"歌手   共 %d 位", count);
    else if (app->view == VIEW_ALBUMS)
        swprintf(title, 512, L"专辑   共 %d 张", count);
    else if (app->filterActive && app->filterKind != FILTER_NONE)
        swprintf(title, 512, L"%ls   共 %d 首   ·   %ls：%ls",
                 app_view_name(app), count,
                 app->filterKind == FILTER_ALBUM ? L"专辑" : L"歌手",
                 app->filterText);
    else if (app->filterActive)
        swprintf(title, 512, L"%ls   搜索结果 %d 首", app_view_name(app), count);
    else
        swprintf(title, 512, L"%ls   共 %d 首", app_view_name(app), count);

    SetWindowTextW(app->hViewTitle, title);

    for (int i = 0; i < HDR_COUNT; i++) {
        const wchar_t *lbl = g_hdr[(int)app->view][i].label;
        if (!lbl) {
            ShowWindow(app->hHdr[i], SW_HIDE);
            continue;
        }
        ShowWindow(app->hHdr[i], SW_SHOW);
        SetWindowTextW(app->hHdr[i], lbl);
    }
}

void ui_refresh_list(App *app)
{
    int i;

    /* 正在播放：盖掉列表，只留自绘的大页面 */
    if (app->view == VIEW_NOWPLAYING) {
        if (app->hNowPlaying) {
            ShowWindow(app->hNowPlaying, SW_SHOW);
            InvalidateRect(app->hNowPlaying, NULL, TRUE);
        }
        ShowWindow(app->hList, SW_HIDE);
        ShowWindow(app->hViewTitle, SW_HIDE);
        for (i = 0; i < HDR_COUNT; i++)
            ShowWindow(app->hHdr[i], SW_HIDE);
        ui_layout(app);
        return;
    }
    if (app->hNowPlaying)
        ShowWindow(app->hNowPlaying, SW_HIDE);
    ShowWindow(app->hList, SW_SHOW);
    ShowWindow(app->hViewTitle, SW_SHOW);

    /* 表头/视图模式随视图变化 */
    if ((app->view == VIEW_ARTISTS || app->view == VIEW_ALBUMS) && app->coverDirty)
        ui_build_covers(app);
    if (app->view != VIEW_ARTISTS && app->view != VIEW_ALBUMS)
        setup_columns(app);
    apply_list_mode(app);
    refresh_view_header(app);
    ui_layout(app);

    SendMessageW(app->hList, WM_SETREDRAW, FALSE, 0);
    ListView_DeleteAllItems(app->hList);

    if (app->view == VIEW_PLAYLISTS) {
        /* 播放列表总览 */
        for (i = 0; i < app->listCount; i++) {
            wchar_t no[16], cnt[32];
            swprintf(no, 16, L"%d", i + 1);
            swprintf(cnt, 32, L"%d", app->lists[i].pl.count);
            insert_row(app, i, no, app->lists[i].name, cnt, L"", L"");
        }
        if (app->listCount == 0) {
            insert_row(app, 0, L"", L"（还没有播放列表，点右上角「新建播放列表」）",
                       L"", L"", L"");
        }
    } else if (app->view == VIEW_ARTISTS || app->view == VIEW_ALBUMS) {
        /* 按歌手 / 专辑：封面卡片网格 */
        for (i = 0; i < app->groupCount; i++) {
            LVITEMW it;
            ZeroMemory(&it, sizeof(it));
            it.mask = LVIF_TEXT | LVIF_IMAGE;
            it.iItem = i;
            it.iImage = i;
            it.pszText = app->groups[i].name;
            SendMessageW(app->hList, LVM_INSERTITEMW, 0, (LPARAM)&it);
        }
        if (app->groupCount == 0) {
            insert_row(app, 0, L"", L"（曲库是空的，先用「添加文件夹」把音乐导进来）",
                       L"", L"", L"");
        }
    } else {
        Playlist *vpl = app_view_pl(app);
        for (i = 0; i < vpl->count; i++) {
            Track *t = &vpl->items[i];
            wchar_t no[16], dur[32], extra1[64], extra2[128];

            swprintf(no, 16, L"%d", i + 1);
            util_format_time(t->length_ms, dur, 32);

            if (app->view == VIEW_RECENT) {
                int c = 0;
                long long lp = 0;
                int k;
                for (k = 0; k < app->recentCount; k++) {
                    if (_wcsicmp(app->recentMeta[k].path, t->path) == 0) {
                        c = app->recentMeta[k].count;
                        lp = app->recentMeta[k].lastPlayed;
                        break;
                    }
                }
                swprintf(extra1, 64, L"%d 次", c);
                fmt_relative(lp, extra2, 128);
                insert_row(app, i, no, t->title, t->artist, extra1, extra2);
            } else {
                insert_row(app, i, no, t->title, t->artist, t->album, dur);
            }
        }
        if (vpl->count == 0) {
            const wchar_t *hint;
            if (app->filterActive && app->searchText[0])
                hint = L"（没有找到匹配的歌曲）";
            else if (app->filterActive)
                hint = L"（这个分组下没有歌曲）";
            else if (app->view == VIEW_RECENT)
                hint = L"（还没有播放记录，听一首歌就有了）";
            else
                hint = L"（列表是空的，用「添加文件夹」把音乐导进来）";
            insert_row(app, 0, L"", hint, L"", L"", L"");
        }
    }

    SendMessageW(app->hList, WM_SETREDRAW, TRUE, 0);
    InvalidateRect(app->hList, NULL, TRUE);
    ui_sync_selection(app);
}

void ui_update_row(App *app, int row)
{
    Playlist *vpl;
    Track *t;
    wchar_t no[16], dur[32];

    if (app->view == VIEW_PLAYLISTS)
        return;
    vpl = app_view_pl(app);
    if (row < 0 || row >= vpl->count)
        return;
    if (row >= (int)SendMessageW(app->hList, LVM_GETITEMCOUNT, 0, 0))
        return;

    t = &vpl->items[row];
    swprintf(no, 16, L"%d", row + 1);
    util_format_time(t->length_ms, dur, 32);

    set_sub(app, row, 0, no);
    set_sub(app, row, 1, t->title);
    set_sub(app, row, 2, t->artist);
    if (app->view != VIEW_RECENT) {
        set_sub(app, row, 3, t->album);
        set_sub(app, row, 4, dur);
    }
}

void ui_sync_selection(App *app)
{
    int row = -1;

    if (app->current >= 0 && app->current < app->pl.count)
        row = ui_find_row_of_path(app, app->pl.items[app->current].path);

    ListView_SetItemState(app->hList, -1, 0, LVIS_SELECTED | LVIS_FOCUSED);
    if (row >= 0) {
        ListView_SetItemState(app->hList, row,
                              LVIS_SELECTED | LVIS_FOCUSED,
                              LVIS_SELECTED | LVIS_FOCUSED);
    }
}

void ui_scroll_to_playing(App *app)
{
    int row;
    if (app->current < 0 || app->current >= app->pl.count)
        return;
    row = ui_find_row_of_path(app, app->pl.items[app->current].path);
    if (row >= 0)
        ListView_EnsureVisible(app->hList, row, FALSE);
}

/* ================= 状态显示 ================= */

void ui_update_times(App *app, int cur, int total)
{
    wchar_t b1[32], b2[32];
    util_format_time(cur, b1, 32);
    util_format_time(total, b2, 32);
    SetWindowTextW(app->hTimeCur, b1);
    SetWindowTextW(app->hTimeTotal, b2);
}

void ui_update_progress(App *app)
{
    int len = 0, pos = 0;

    if (app->current < 0 || !audio_is_open(app->audio)) {
        if (!app->dragging)
            SendMessageW(app->hProgress, TBM_SETPOS, TRUE, 0);
        ui_update_times(app, -1, -1);
        return;
    }

    if (!audio_get_length(app->audio, &len) || len <= 0)
        len = app->pl.items[app->current].length_ms;

    if (app->dragging) {
        int p = (int)SendMessageW(app->hProgress, TBM_GETPOS, 0, 0);
        pos = (len > 0) ? (int)((long long)len * p / 1000) : 0;
    } else {
        if (!audio_get_position(app->audio, &pos))
            pos = 0;
        if (len > 0) {
            int p = (int)((long long)pos * 1000 / len);
            if (p < 0) p = 0;
            if (p > 1000) p = 1000;
            SendMessageW(app->hProgress, TBM_SETPOS, TRUE, p);
        }
    }
    ui_update_times(app, pos, len > 0 ? len : -1);
}

void ui_update_play_button(App *app)
{
    const wchar_t *want = audio_is_playing(app->audio) ? L"暂停" : L"播放";
    wchar_t cur[16];

    cur[0] = 0;
    GetWindowTextW(app->hBtn[BTN_PLAY], cur, 16);
    if (wcscmp(cur, want) != 0)
        SetWindowTextW(app->hBtn[BTN_PLAY], want);
}

void ui_update_mode_button(App *app)
{
    HMENU hMenu = GetMenu(app->hMain);
    UINT ids[4] = { IDM_MODE_SEQ, IDM_MODE_LOOP_ALL, IDM_MODE_LOOP_ONE, IDM_MODE_SHUFFLE };
    int m = (int)app->mode;
    if (m < 0 || m > 3) m = 0;

    SetWindowTextW(app->hBtn[BTN_MODE], g_mode_names[m]);

    if (hMenu) {
        HMENU hPlay = GetSubMenu(hMenu, 1);
        if (hPlay) {
            HMENU hMode = GetSubMenu(hPlay, 6);
            if (hMode)
                CheckMenuRadioItem(hMode, IDM_MODE_SEQ, IDM_MODE_SHUFFLE, ids[m], MF_BYCOMMAND);
        }
    }
}

void ui_update_status(App *app, const wchar_t *extra)
{
    wchar_t buf[768];
    const wchar_t *state = audio_is_playing(app->audio) ? L"播放中" : L"已停止";
    const wchar_t *name = L"（未选择）";

    if (app->current >= 0 && app->current < app->pl.count)
        name = app->pl.items[app->current].title;

    swprintf(buf, 768, L"%ls  |  %ls  |  队列 %d 首  |  %ls  |  音量 %d%%%ls%ls",
             state, name, app->pl.count, g_mode_names[(int)app->mode], app->volume,
             (extra && *extra) ? L"  |  " : L"",
             (extra && *extra) ? extra : L"");
    SendMessageW(app->hStatus, SB_SETTEXTW, 0, (LPARAM)buf);
}

void ui_update_lyric(App *app, int force)
{
    int pos = 0;
    if (!app->hLyric || !app->showLyric)
        return;
    if (audio_get_position(app->audio, &pos) || force)
        lyricview_set_time(app->hLyric, pos);
}

/* ================= 事件 ================= */

void ui_on_list_dblclk(App *app, int row)
{
    if (row < 0)
        return;
    app_play_from_view(app, row);
}

void ui_on_hscroll(App *app, HWND hCtl, int code)
{
    if (hCtl == app->hVolume) {
        int v = (int)SendMessageW(app->hVolume, TBM_GETPOS, 0, 0);
        app_set_volume(app, v);
        return;
    }

    if (hCtl == app->hProgress) {
        switch (code) {
        case TB_THUMBTRACK:
        case TB_LINEUP:
        case TB_LINEDOWN:
        case TB_PAGEUP:
        case TB_PAGEDOWN:
        case TB_TOP:
        case TB_BOTTOM:
            app->dragging = 1;
            ui_update_progress(app);
            break;

        case TB_THUMBPOSITION:
        case TB_ENDTRACK:
        default:
            if (app->dragging) {
                int len = 0;
                int p = (int)SendMessageW(app->hProgress, TBM_GETPOS, 0, 0);
                app->dragging = 0;
                if (app->current >= 0) {
                    if (!audio_get_length(app->audio, &len) || len <= 0)
                        len = app->pl.items[app->current].length_ms;
                    if (len > 0)
                        audio_seek(app->audio, (int)((long long)len * p / 1000));
                }
                ui_update_progress(app);
            }
            break;
        }
    }
}

int ui_on_command(App *app, int id)
{
    /* 视图头部按钮：ID 是固定的 4 个，要按当前视图转发到对应的命令 */
    if (id >= IDC_HDR1 && id < IDC_HDR1 + HDR_COUNT) {
        int i = id - IDC_HDR1;
        int cmd = g_hdr[(int)app->view][i].cmd;
        if (g_hdr[(int)app->view][i].label && cmd)
            return ui_on_command(app, cmd);
        return 1;
    }

    switch (id) {
    case IDC_BTN_OPENF:
    case IDM_FILE_OPEN_FILES:   app_open_files(app); return 1;
    case IDC_BTN_ADDFOLD:
    case IDM_FILE_ADD_FOLDER:   app_add_folder(app); return 1;
    case IDM_FILE_SCAN_MUSIC:   app_scan_music_folder(app); return 1;
    case IDM_FILE_CLEAR_LIB:    app_clear_library(app); return 1;
    case IDM_EDIT_REMOVE:
    case IDM_LIST_REMOVE:       app_remove_selected(app); return 1;

    case IDM_FILE_EXIT:         DestroyWindow(app->hMain); return 1;

    case IDC_BTN_PLAY:
    case IDM_PLAY_TOGGLE:       app_toggle_play(app); return 1;
    case IDC_BTN_STOP:
    case IDM_PLAY_STOP:         app_stop(app); return 1;
    case IDC_BTN_NEXT:
    case IDM_PLAY_NEXT:         app_next(app, 1); return 1;
    case IDC_BTN_PREV:
    case IDM_PLAY_PREV:         app_prev(app); return 1;
    case IDM_PLAY_ALL:          app_play_all_visible(app); return 1;

    case IDC_BTN_MODE:
        app_set_mode(app, (PlayMode)(((int)app->mode + 1) % 4));
        return 1;
    case IDM_MODE_SEQ:          app_set_mode(app, MODE_SEQUENTIAL); return 1;
    case IDM_MODE_LOOP_ALL:     app_set_mode(app, MODE_LOOP_ALL);   return 1;
    case IDM_MODE_LOOP_ONE:     app_set_mode(app, MODE_LOOP_ONE);   return 1;
    case IDM_MODE_SHUFFLE:      app_set_mode(app, MODE_SHUFFLE);    return 1;

    case IDC_BTN_LYRIC:
    case IDM_VIEW_LYRIC:
        app->showLyric = !app->showLyric;
        store_set_int(L"showLyric", app->showLyric);
        ShowWindow(app->hLyric, app->showLyric ? SW_SHOW : SW_HIDE);
        ui_layout(app);
        ui_update_lyric(app, 1);
        {
            HMENU hMenu = GetMenu(app->hMain);
            if (hMenu) {
                HMENU hView = GetSubMenu(hMenu, 3);
                if (hView)
                    CheckMenuItem(hView, IDM_VIEW_LYRIC,
                                  MF_BYCOMMAND | (app->showLyric ? MF_CHECKED : MF_UNCHECKED));
            }
        }
        return 1;

    case IDM_VIEW_LIBRARY:      app_set_view(app, VIEW_LIBRARY, 0); return 1;
    case IDM_VIEW_ARTISTS:
        app_set_view(app, VIEW_ARTISTS, 0);
        app_build_groups(app, 0);
        app_refresh_view(app);
        return 1;
    case IDM_VIEW_ALBUMS:
        app_set_view(app, VIEW_ALBUMS, 0);
        app_build_groups(app, 1);
        app_refresh_view(app);
        return 1;
    case IDM_VIEW_FAVORITES:    app_set_view(app, VIEW_FAVORITES, 0); return 1;
    case IDM_VIEW_NOWPLAYING:   app_set_view(app, VIEW_NOWPLAYING, 0); return 1;
    case IDM_FAV_TOGGLE:        app_toggle_favorite(app); return 1;
    case IDM_LYRIC_OPEN:        app_open_lyric_file(app); return 1;
    case IDM_VIEW_PLAYLISTS:    app_set_view(app, VIEW_PLAYLISTS, 0); return 1;
    case IDM_VIEW_RECENT:       app_set_view(app, VIEW_RECENT, 0); return 1;
    case IDM_VIEW_CLEAR_RECENT: app_clear_recent(app); return 1;
    case IDM_VIEW_REFRESH:      app_refresh_view(app); return 1;

    case IDM_LIST_NEW:          app_new_playlist(app); return 1;
    case IDM_LIST_ADD_CURRENT:  app_add_selected_to_playlist(app); return 1;
    case IDM_LIST_RENAME:       app_rename_current_playlist(app); return 1;
    case IDM_LIST_DELETE:       app_delete_current_playlist(app); return 1;
    case IDM_LIST_SAVE_AS:      app_save_current_playlist(app); return 1;
    case IDM_LIST_LOAD_FILE:    app_load_playlist_file(app); return 1;

    case IDM_HELP_ABOUT:
        MessageBoxW(app->hMain,
            APP_NAME_TEXT L"  " APP_VERSION_TEXT L"  (" APP_BUILD_TAG L")\n\n"
            L"纯 C + Win32 API。音频：Media Foundation 解码 + waveOut 输出。\n"
            L"音量：WASAPI 按进程音频会话。标签：自解析 ID3。歌词：自解析 LRC。\n\n"
            L"我的歌曲、播放列表、最近播放都会自动保存到程序目录的 data\\ 下。\n"
            L"歌词请把同名的 .lrc 文件放在歌曲旁边。",
            L"关于", MB_OK | MB_ICONINFORMATION);
        return 1;
    }
    return 0;
}

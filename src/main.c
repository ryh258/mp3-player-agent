/* main.c —— 程序入口：菜单、窗口过程、消息循环 */
#include "app.h"
#include "crash.h"
#include "cover.h"
#include "nav.h"
#include "nowplaying.h"
#include "splash.h"
#include "lyricview.h"
#include "theme.h"
#include "util.h"
#include "store.h"
#include "tag_id3.h"
#include <stdio.h>

#include <commctrl.h>
#include <windows.h>
#include <wchar.h>
#include <string.h>

#define APP_CLASS   L"MP3PlayerWnd"
#define APP_TITLE   APP_NAME_TEXT L"  " APP_VERSION_TEXT L"  " APP_BUILD_TAG
#define WIN_W       1220
#define WIN_H       760
#define MIN_W       940
#define MIN_H       540

/* 菜单在顶级菜单里的下标（ui.c 会按这个找子菜单） */
#define TOP_FILE   0
#define TOP_PLAY   1
#define TOP_LIST   2
#define TOP_VIEW   3
#define TOP_HELP   4

/* ------------------------------------------------------------------ */
/* 菜单                                                                */
/* ------------------------------------------------------------------ */

static HMENU build_menu(void)
{
    HMENU hMenu = CreateMenu();
    HMENU hFile = CreatePopupMenu();
    HMENU hPlay = CreatePopupMenu();
    HMENU hMode = CreatePopupMenu();
    HMENU hList = CreatePopupMenu();
    HMENU hView = CreatePopupMenu();
    HMENU hHelp = CreatePopupMenu();

    AppendMenuW(hFile, MF_STRING, IDM_FILE_OPEN_FILES, L"打开文件...\tCtrl+O");
    AppendMenuW(hFile, MF_STRING, IDM_FILE_ADD_FOLDER, L"添加文件夹到曲库...\tCtrl+D");
    AppendMenuW(hFile, MF_STRING, IDM_FILE_SCAN_MUSIC, L"扫描系统音乐文件夹");
    AppendMenuW(hFile, MF_SEPARATOR, 0, NULL);
    AppendMenuW(hFile, MF_STRING, IDM_FILE_CLEAR_LIB, L"清空曲库");
    AppendMenuW(hFile, MF_SEPARATOR, 0, NULL);
    AppendMenuW(hFile, MF_STRING, IDM_LYRIC_OPEN, L"给当前歌曲指定歌词文件...");
    AppendMenuW(hFile, MF_SEPARATOR, 0, NULL);
    AppendMenuW(hFile, MF_STRING, IDM_FILE_EXIT, L"退出");

    AppendMenuW(hPlay, MF_STRING, IDM_PLAY_TOGGLE, L"播放 / 暂停\t空格");
    AppendMenuW(hPlay, MF_STRING, IDM_PLAY_STOP, L"停止");
    AppendMenuW(hPlay, MF_SEPARATOR, 0, NULL);
    AppendMenuW(hPlay, MF_STRING, IDM_PLAY_PREV, L"上一首");
    AppendMenuW(hPlay, MF_STRING, IDM_PLAY_NEXT, L"下一首");
    AppendMenuW(hPlay, MF_SEPARATOR, 0, NULL);
    AppendMenuW(hMode, MF_STRING, IDM_MODE_SEQ, L"顺序播放");
    AppendMenuW(hMode, MF_STRING, IDM_MODE_LOOP_ALL, L"列表循环");
    AppendMenuW(hMode, MF_STRING, IDM_MODE_LOOP_ONE, L"单曲循环");
    AppendMenuW(hMode, MF_STRING, IDM_MODE_SHUFFLE, L"随机播放");
    AppendMenuW(hPlay, MF_STRING | MF_POPUP, (UINT_PTR)hMode, L"播放模式");

    AppendMenuW(hList, MF_STRING, IDM_LIST_NEW, L"新建播放列表...");
    AppendMenuW(hList, MF_STRING, IDM_LIST_ADD_CURRENT, L"把选中的歌添加到播放列表...");
    AppendMenuW(hList, MF_STRING, IDM_LIST_REMOVE, L"从当前列表移除选中\tDelete");
    AppendMenuW(hList, MF_SEPARATOR, 0, NULL);
    AppendMenuW(hList, MF_STRING, IDM_LIST_RENAME, L"重命名当前播放列表...");
    AppendMenuW(hList, MF_STRING, IDM_LIST_DELETE, L"删除当前播放列表");
    AppendMenuW(hList, MF_SEPARATOR, 0, NULL);
    AppendMenuW(hList, MF_STRING, IDM_LIST_SAVE_AS, L"导出为 m3u 文件...");
    AppendMenuW(hList, MF_STRING, IDM_LIST_LOAD_FILE, L"从 m3u 文件导入...");

    AppendMenuW(hView, MF_STRING, IDM_VIEW_LIBRARY, L"我的歌曲\tCtrl+1");
    AppendMenuW(hView, MF_STRING, IDM_VIEW_ARTISTS, L"按歌手浏览\tCtrl+2");
    AppendMenuW(hView, MF_STRING, IDM_VIEW_ALBUMS, L"按专辑浏览\tCtrl+3");
    AppendMenuW(hView, MF_SEPARATOR, 0, NULL);
    AppendMenuW(hView, MF_STRING, IDM_VIEW_PLAYLISTS, L"播放列表");
    AppendMenuW(hView, MF_STRING, IDM_VIEW_RECENT, L"最近播放");
    AppendMenuW(hView, MF_SEPARATOR, 0, NULL);
    AppendMenuW(hView, MF_STRING, IDM_VIEW_LYRIC, L"显示歌词面板\tCtrl+L");
    AppendMenuW(hView, MF_STRING, IDM_VIEW_REFRESH, L"刷新\tF5");

    AppendMenuW(hHelp, MF_STRING, IDM_HELP_ABOUT, L"关于");

    AppendMenuW(hMenu, MF_POPUP, (UINT_PTR)hFile, L"文件");
    AppendMenuW(hMenu, MF_POPUP, (UINT_PTR)hPlay, L"播放");
    AppendMenuW(hMenu, MF_POPUP, (UINT_PTR)hList, L"播放列表");
    AppendMenuW(hMenu, MF_POPUP, (UINT_PTR)hView, L"视图");
    AppendMenuW(hMenu, MF_POPUP, (UINT_PTR)hHelp, L"帮助");

    return hMenu;
}

/* ------------------------------------------------------------------ */
/* 窗口过程                                                            */
/* ------------------------------------------------------------------ */

static LRESULT CALLBACK WndProc(HWND hWnd, UINT msg, WPARAM wp, LPARAM lp)
{
    App *app = &g_app;

    if (app->audio && audio_handle_message(app->audio, msg, wp, lp))
        return 0;

    switch (msg) {

    case WM_ERASEBKGND: {
        /* 顶部工具栏条画深蓝，其余画浅底 —— 和设计稿一致 */
        HDC dc = (HDC)wp;
        RECT rcAll;
        int tbH;
        GetClientRect(hWnd, &rcAll);
        tbH = ui_toolbar_height(app);
        {
            RECT t2 = rcAll;
            t2.bottom = tbH;
            theme_fill_vgrad(dc, &t2, TH_TOOLBAR, TH_TOOLBAR);
        }
        {
            RECT b2 = rcAll;
            b2.top = tbH;
            HBRUSH br = CreateSolidBrush(TH_BG);
            FillRect(dc, &b2, br);
            DeleteObject(br);
        }
        return 1;
    }

    case WM_SIZE:
        if (app->hList)
            ui_layout(app);
        InvalidateRect(hWnd, NULL, TRUE);
        return 0;

    case WM_GETMINMAXINFO:
        ((MINMAXINFO *)lp)->ptMinTrackSize.x = MulDiv(MIN_W, app->dpi ? app->dpi : 96, 96);
        ((MINMAXINFO *)lp)->ptMinTrackSize.y = MulDiv(MIN_H, app->dpi ? app->dpi : 96, 96);
        return 0;

    case WM_COMMAND: {
        WORD id = LOWORD(wp);
        WORD code = HIWORD(wp);

        /* 左侧导航点击 */
        if (code == NAV_NOTIFY) {
            app_on_nav_click(app, (int)id);
            return 0;
        }
        /* 歌词行点击 */
        if (code == LYRIC_NOTIFY) {
            app_on_lyric_click(app, (int)id);
            return 0;
        }
        /* 搜索框内容变化 */
        if (code == EN_CHANGE && id == IDC_SEARCH) {
            wchar_t buf[GROUP_NAME_MAX];
            GetWindowTextW(app->hSearch, buf, GROUP_NAME_MAX);
            app_set_search(app, buf);
            return 0;
        }
        if (code == 0 || code == 1) {          /* 菜单 / 按钮 */
            if (ui_on_command(app, (int)id))
                return 0;
        }
        break;
    }

    case WM_HSCROLL:
        if (app->hProgress || app->hVolume)
            ui_on_hscroll(app, (HWND)lp, (int)LOWORD(wp));
        return 0;

    case WM_NOTIFY: {
        NMHDR *nh = (NMHDR *)lp;

        if (app->hList && nh->hwndFrom == app->hList) {
            if (nh->code == NM_DBLCLK) {
                NMITEMACTIVATE *ia = (NMITEMACTIVATE *)lp;
                ui_on_list_dblclk(app, ia->iItem);
                return 0;
            }
            if (nh->code == NM_RETURN) {
                int idx = ListView_GetNextItem(app->hList, -1, LVNI_SELECTED);
                ui_on_list_dblclk(app, idx);
                return 0;
            }
            if (nh->code == LVN_KEYDOWN) {
                NMLVKEYDOWN *kd = (NMLVKEYDOWN *)lp;
                if (kd->wVKey == VK_DELETE) {
                    PostMessageW(hWnd, WM_COMMAND, IDM_EDIT_REMOVE, 0);
                    return 0;
                }
                if (kd->wVKey == VK_SPACE) {
                    app_toggle_play(app);
                    return 0;
                }
            }
        }
        break;
    }

    case WM_TIMER:
        if (wp == IDT_TICK)
            app_on_tick(app);
        return 0;

    case WM_CTLCOLORSTATIC:
        SetBkMode((HDC)wp, TRANSPARENT);
        return (LRESULT)GetSysColorBrush(COLOR_BTNFACE);

    case WM_CLOSE:
        DestroyWindow(hWnd);
        return 0;

    case WM_DESTROY:
        KillTimer(hWnd, IDT_TICK);
        PostQuitMessage(0);
        return 0;
    }

    return DefWindowProcW(hWnd, msg, wp, lp);
}

/* ------------------------------------------------------------------ */
/* 入口                                                                */
/* ------------------------------------------------------------------ */

int WINAPI wWinMain(HINSTANCE hInst, HINSTANCE hPrev, LPWSTR lpCmd, int nShow)
{
    WNDCLASSEXW wc;
    INITCOMMONCONTROLSEX icc;
    ACCEL acc[6];
    HACCEL hAcc;
    HWND hWnd;
    MSG msg;

    (void)hPrev;

    /* 必须赶在任何窗口创建之前：让进程按显示器 DPI 缩放。
       清单里也声明了 DPI 感知，但 Win11 上老写法的 dpiAware 不一定被采纳，
       这里是运行时保险（已经感知过则调用失败，无副作用）。 */
    SetProcessDPIAware();

    icc.dwSize = sizeof(icc);
    icc.dwICC = ICC_LISTVIEW_CLASSES | ICC_BAR_CLASSES |
                ICC_PROGRESS_CLASS | ICC_STANDARD_CLASSES;
    InitCommonControlsEx(&icc);

    crash_install();
    cover_startup();      /* GDI+：用来解码内嵌的专辑封面 */

    /* 诊断：把封面提取的结果写进 coverdump.log（排查封面显示不出来的问题） */
    if (lpCmd && wcsstr(lpCmd, L"--coverdump")) {
        Playlist pl; int i; FILE *f;
        playlist_init(&pl);
        store_init();
        store_load_library(&pl);
        f = _wfopen(L"coverdump.log", L"w, ccs=UTF-8");
        if (f) {
            fwprintf(f, L"cover_startup = %d\n", cover_startup());
            fwprintf(f, L"library = %d 首\n", pl.count);
            for (i = 0; i < pl.count; i++) {
                unsigned char *dd = NULL; unsigned nn = 0;
                int ok = id3_cover(pl.items[i].path, &dd, &nn);
                fwprintf(f, L"[%d] id3_cover=%d len=%u", i, ok, nn);
                if (ok) {
                    HBITMAP bm = cover_from_memory(dd, (int)nn, 156);
                    fwprintf(f, L"  cover_from_memory=%s", bm ? L"OK" : L"NULL");
                    if (bm) {
                        BITMAP bi; GetObjectW(bm, sizeof(bi), &bi);
                        fwprintf(f, L" (%dx%d)", (int)bi.bmWidth, (int)bi.bmHeight);
                        DeleteObject(bm);
                    }
                    if (nn > 4)
                        fwprintf(f, L"  head=%02X%02X%02X%02X", dd[0], dd[1], dd[2], dd[3]);
                    free(dd);
                }
                fwprintf(f, L"\n");
            }
            fclose(f);
        }
        playlist_free(&pl);
        return 0;
    }
    if (lpCmd && wcsstr(lpCmd, L"--selftest"))
        return selftest_run(hInst, lpCmd && wcsstr(lpCmd, L"--quiet") ? 1 : 0);

    nav_register_class(hInst);
    lyricview_register_class(hInst);
    nowplaying_register_class(hInst);

    ZeroMemory(&wc, sizeof(wc));
    wc.cbSize        = sizeof(wc);
    wc.style         = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc   = WndProc;
    wc.hInstance     = hInst;
    wc.hIcon         = LoadIconW(hInst, MAKEINTRESOURCEW(IDI_APPICON));
    if (!wc.hIcon)
        wc.hIcon = LoadIconW(NULL, IDI_APPLICATION);
    wc.hIconSm       = wc.hIcon;
    wc.hCursor       = LoadCursorW(NULL, IDC_ARROW);
    wc.hbrBackground = CreateSolidBrush(TH_TOOLBAR);   /* 兜底：工具栏那条深蓝 */
    wc.lpszClassName = APP_CLASS;

    if (!RegisterClassExW(&wc))
        return 1;

    /* 先取系统 DPI，布局和窗口尺寸都要按它缩放 */
    {
        HDC dc = GetDC(NULL);
        int dpi = dc ? GetDeviceCaps(dc, LOGPIXELSX) : 96;
        if (dc) ReleaseDC(NULL, dc);
        g_app.dpi = (dpi > 0) ? dpi : 96;
    }

    hWnd = CreateWindowExW(0, APP_CLASS, APP_TITLE,
                           WS_OVERLAPPEDWINDOW,
                           CW_USEDEFAULT, CW_USEDEFAULT,
                           MulDiv(WIN_W, g_app.dpi, 96),
                           MulDiv(WIN_H, g_app.dpi, 96),
                           NULL, build_menu(), hInst, NULL);
    if (!hWnd)
        return 1;

    app_init(&g_app, hInst, hWnd);
    ui_create(&g_app);

    app_rebuild_nav(&g_app);
    {
        int added = (lpCmd && *lpCmd) ? app_open_paths(&g_app, lpCmd) : 0;
        if (added == 0)
            ui_refresh_list(&g_app);
    }
    ui_layout(&g_app);
    ui_update_play_button(&g_app);
    ui_update_mode_button(&g_app);
    ui_update_times(&g_app, -1, -1);
    ui_update_status(&g_app, NULL);

    if (g_app.showLyric) {
        HMENU hMenu = GetMenu(hWnd);
        HMENU hView = hMenu ? GetSubMenu(hMenu, TOP_VIEW) : NULL;
        if (hView)
            CheckMenuItem(hView, IDM_VIEW_LYRIC, MF_BYCOMMAND | MF_CHECKED);
    }

    splash_show(hInst, g_app.dpi, 1500);

    SetTimer(hWnd, IDT_TICK, 500, NULL);

    ShowWindow(hWnd, nShow);
    UpdateWindow(hWnd);

    acc[0].fVirt = FVIRTKEY | FCONTROL; acc[0].key = 'O';      acc[0].cmd = IDM_FILE_OPEN_FILES;
    acc[1].fVirt = FVIRTKEY | FCONTROL; acc[1].key = 'D';      acc[1].cmd = IDM_FILE_ADD_FOLDER;
    acc[2].fVirt = FVIRTKEY;            acc[2].key = VK_SPACE; acc[2].cmd = IDM_PLAY_TOGGLE;
    acc[3].fVirt = FVIRTKEY | FCONTROL; acc[3].key = '1';      acc[3].cmd = IDM_VIEW_LIBRARY;
    acc[4].fVirt = FVIRTKEY | FCONTROL; acc[4].key = '2';      acc[4].cmd = IDM_VIEW_ARTISTS;
    acc[5].fVirt = FVIRTKEY | FCONTROL; acc[5].key = '3';      acc[5].cmd = IDM_VIEW_ALBUMS;
    hAcc = CreateAcceleratorTableW(acc, 6);

    while (GetMessageW(&msg, NULL, 0, 0) > 0) {
        if (hAcc && TranslateAcceleratorW(hWnd, hAcc, &msg))
            continue;
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    if (hAcc)
        DestroyAcceleratorTable(hAcc);

    app_free(&g_app);
    cover_shutdown();
    return (int)msg.wParam;
}

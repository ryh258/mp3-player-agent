/* selftest.c —— 音频后端自检（MP3Player.exe --selftest）
 *
 * 为什么内建在播放器里：
 *   本机 Smart App Control 会拦掉部分新编译的未签名 exe（按文件哈希随机判定），
 *   单独编一个探针程序经常跑不起来。放进主程序就用同一个 exe，最省事。
 *
 * 作用：在没有 GUI 交互的情况下，验证 MCI 后端的
 *       打开 / 时长 / 播放 / 位置推进 / 暂停冻结 / 跳转 / 停止 是否真的有效。
 * 结果：写到 exe 同目录的 selftest.log，并弹出摘要对话框。
 */
#include "app.h"
#include "audio.h"
#include "appvol.h"
#include "tag_id3.h"
#include "util.h"

#include <stdio.h>
#include <wchar.h>

static FILE *g_log = NULL;

#define L(...) do { if (g_log) { fwprintf(g_log, __VA_ARGS__); fflush(g_log); } } while (0)

static int g_pass = 0;
static int g_fail = 0;
static int g_total = 0;
static int g_peakSeen = 0;      /* 有几个文件真的检测到音频输出 */

static int test_one(Audio *a, const wchar_t *path)
{
    int len = -1, pos = -1, i, ok = 1;
    int p1 = -1, p2 = -1;
    float peak = 0.0f, peakMax = 0.0f;

    g_total++;
    L(L"\n--- %ls\n", path);

    /* 先验证 ID3 标签解析（列表里显示什么全靠它） */
    {
        Id3Tags tags;
        if (id3_read(path, &tags)) {
            L(L"  标签: 标题='%ls'  歌手='%ls'  专辑='%ls'  年份='%ls'  音轨=%d"
              L"  (v2=%d v1=%d)\n",
              tags.title, tags.artist, tags.album, tags.year,
              tags.track, tags.hasV2, tags.hasV1);
        } else {
            L(L"  标签: 没有 ID3 标签（列表里会显示文件名）\n");
        }
    }

    if (!audio_open(a, path)) {
        L(L"  [失败] open 打不开: %ls\n", audio_last_error(a));
        g_fail++;
        return 0;
    }
    L(L"  [OK]   open\n");

    if (audio_get_length(a, &len) && len > 0) {
        L(L"  [OK]   时长 = %d ms (%.2f 秒)\n", len, len / 1000.0);
    } else {
        L(L"  [失败] status length 取不到\n");
        ok = 0;
    }

    /* 注意：音量要等播放开始、本进程音频会话建立之后才能设置，
       所以音量测试放在 play 之后。 */
    if (!audio_play(a)) {
        L(L"  [失败] play: %ls\n", audio_last_error(a));
        ok = 0;
    } else {
        L(L"  [OK]   play\n");
        for (i = 0; i < 3; i++) {
            Sleep(500);
            pos = -1;
            audio_get_position(a, &pos);
            peak = 0.0f;
            appvol_peak(&peak);
            if (peak > peakMax) peakMax = peak;
            L(L"         +%dms  位置=%d  播放中=%d  峰值=%.3f\n",
              (i + 1) * 500, pos, audio_is_playing(a), peak);
        }
        if (pos <= 0) {
            L(L"  [失败] 位置没有推进，可能没真的出声\n");
            ok = 0;
        }
    }

    /* 音量：播放开始后会话已建立，这条路径必须能通 */
    if (audio_set_volume(a, 50))
        L(L"  [OK]   音量设为 50%%\n");
    else {
        L(L"  [失败] 音量设置失败: %ls\n", audio_last_error(a));
        ok = 0;
    }

    /* 直接验证 WASAPI 会话本身 */
    {
        int v = -1;
        if (appvol_set(30)) {
            appvol_get(&v);
            L(L"  [OK]   WASAPI 会话音量设为 30%%，读回 %d%%\n", v);
            audio_set_volume(a, 50);
        } else {
            L(L"  [失败] 找不到本进程音频会话，音量无法按进程调节\n");
            ok = 0;
        }
    }

    /* 暂停后位置应冻结 */
    audio_pause(a);
    Sleep(300);
    audio_get_position(a, &p1);
    Sleep(600);
    audio_get_position(a, &p2);
    if (p1 == p2) {
        L(L"  [OK]   暂停已冻结位置 (%d ms)\n", p1);
    } else {
        L(L"  [失败] 暂停后位置仍在走 %d -> %d\n", p1, p2);
        ok = 0;
    }

    /* 跳转 */
    pos = -1;
    if (audio_seek(a, 1500)) {
        Sleep(150);
        audio_play(a);
        Sleep(700);
        audio_get_position(a, &pos);
        peak = 0.0f;
        appvol_peak(&peak);
        if (peak > peakMax) peakMax = peak;
        if (pos >= 1400)
            L(L"  [OK]   seek 1500ms 后位置=%d\n", pos);
        else {
            L(L"  [警告] seek 后位置=%d，期望 >= 1400\n", pos);
        }
    } else {
        L(L"  [失败] seek: %ls\n", audio_last_error(a));
        ok = 0;
    }

    /* 峰值 > 0 说明音频真的送到声卡了（不只是"位置在走"） */
    if (peakMax > 0.0005f) {
        L(L"  [OK]   检测到音频输出，峰值 %.3f\n", peakMax);
        g_peakSeen++;
    } else {
        L(L"  [提示] 全程峰值为 0（这一段可能本身就是静音）\n");
    }

    audio_stop(a);
    Sleep(150);
    audio_close(a);

    L(L"  => %ls\n", ok ? L"通过" : L"有问题");
    if (ok) g_pass++; else g_fail++;
    return ok;
}

/* 取 exe 所在目录 */
static void exe_dir(wchar_t *out, size_t cch)
{
    wchar_t *p;
    GetModuleFileNameW(NULL, out, (DWORD)cch);
    p = wcsrchr(out, L'\\');
    if (p) *p = 0;
}

static int scan_and_test(Audio *a, const wchar_t *dir)
{
    static const wchar_t *patterns[] = { L"*.wav", L"*.mp3", L"*.m4a", L"*.wma" };
    int found = 0;
    size_t k;

    for (k = 0; k < sizeof(patterns) / sizeof(patterns[0]); k++) {
        wchar_t pat[TRACK_PATH_MAX];
        WIN32_FIND_DATAW fd;
        HANDLE h;

        swprintf(pat, TRACK_PATH_MAX, L"%ls\\%ls", dir, patterns[k]);
        h = FindFirstFileW(pat, &fd);
        if (h == INVALID_HANDLE_VALUE)
            continue;
        do {
            wchar_t full[TRACK_PATH_MAX];
            if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
                continue;
            swprintf(full, TRACK_PATH_MAX, L"%ls\\%ls", dir, fd.cFileName);
            test_one(a, full);
            found++;
        } while (FindNextFileW(h, &fd));
        FindClose(h);
    }
    return found;
}

int selftest_run(HINSTANCE hInst, int quiet)
{
    wchar_t dir[TRACK_PATH_MAX];
    wchar_t logPath[TRACK_PATH_MAX];
    wchar_t msg[1024];
    HWND hMsg;
    Audio *a;
    int found;

    (void)hInst;

    exe_dir(dir, TRACK_PATH_MAX);
    swprintf(logPath, TRACK_PATH_MAX, L"%ls\\selftest.log", dir);
    g_log = _wfopen(logPath, L"w, ccs=UTF-8");
    if (!g_log) {
        MessageBoxW(NULL, L"无法写入 selftest.log", L"自检", MB_ICONERROR);
        return 1;
    }

    L(L"=== MP3 播放器 音频后端自检 ===\n");
    L(L"可执行文件目录: %ls\n", dir);

    hMsg = CreateWindowExW(0, L"STATIC", L"mp3selftest", WS_OVERLAPPED,
                           0, 0, 100, 100, NULL, NULL, NULL, NULL);
    a = audio_create(hMsg);
    if (!a) {
        L(L"audio_create 失败\n");
        fclose(g_log);
        return 1;
    }

    /* 优先测 test_music 子目录，其次测 exe 同目录 */
    {
        wchar_t tdir[TRACK_PATH_MAX];
        swprintf(tdir, TRACK_PATH_MAX, L"%ls\\test_music", dir);
        found = scan_and_test(a, tdir);
        if (found == 0)
            found = scan_and_test(a, dir);
    }

    audio_destroy(a);
    if (hMsg)
        DestroyWindow(hMsg);

    if (found == 0) {
        L(L"\n没有找到可测试的音频文件（test_music 目录为空）。\n");
        L(L"请拷几首 MP3 到 test_music 目录后重试。\n");
    }

    L(L"\n=== 结果: 共 %d 个文件, 通过 %d, 失败 %d ===\n",
      g_total, g_pass, g_fail);
    L(L"=== 其中 %d 个文件检测到真实音频输出（峰值 > 0）===\n", g_peakSeen);
    if (g_total > 0 && g_peakSeen == 0)
        L(L"!!! 一个都没检测到音频输出 —— 请检查系统音量/输出设备 !!!\n");

    swprintf(msg, 1024,
        L"自检完成\n\n"
        L"测试文件: %d 个\n"
        L"通过: %d\n"
        L"失败: %d\n"
        L"检测到真实音频输出: %d 个\n\n"
        L"详细日志:\n%ls",
        g_total, g_pass, g_fail, g_peakSeen, logPath);

    fclose(g_log);
    g_log = NULL;

    if (!quiet)
        MessageBoxW(NULL, msg, L"音频后端自检", MB_OK | MB_ICONINFORMATION);
    return g_fail > 0 ? 1 : 0;
}

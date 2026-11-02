/* audio_mci.c —— 用 Windows MCI 实现的音频后端（Alpha 阶段）
 *
 * 为什么选 MCI：
 *   本机不能联网，第三方解码库（miniaudio / dr_mp3 / libmpg123）一个都拿不到。
 *   MCI 走系统自带的 mciqtz32.dll（DirectShow 封装），注册表里
 *   mp3 -> MPEGVideo、wav -> WaveAudio 已经映射好，零依赖即可播放。
 *
 * 局限（Beta 换 Media Foundation 的原因）：
 *   拿不到 PCM 数据，所以做不了频谱可视化、图形均衡器、精确淡入淡出。
 */
#include "audio.h"
#include "appvol.h"

#include <mmsystem.h>
#include <objbase.h>
#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>
#include <wchar.h>

#define MCI_ALIAS   L"mp3p"
#define MCI_CMD_MAX 1024

struct Audio {
    HWND    notify;          /* 接收 MM_MCINOTIFY 的窗口 */
    int     open;
    int     volume;          /* 0..100，跨曲目记忆 */
    int     ended;           /* 自然播完标志 */
    DWORD   lastErr;
    wchar_t errText[256];
};

static void set_err(Audio *a, MCIERROR e)
{
    a->lastErr = (DWORD)e;
    a->errText[0] = 0;
    if (e)
        mciGetErrorStringW(e, a->errText, 256);
}

/* 拼命令并发送。cb 非空时作为 command 的回调窗口（play ... notify 用）。 */
static MCIERROR mci_cmd(Audio *a, HWND cb, LPWSTR out, UINT cchOut,
                        const wchar_t *fmt, ...)
{
    wchar_t cmd[MCI_CMD_MAX];
    va_list ap;
    MCIERROR e;

    va_start(ap, fmt);
    _vsnwprintf(cmd, MCI_CMD_MAX - 1, fmt, ap);
    va_end(ap);
    cmd[MCI_CMD_MAX - 1] = 0;

    e = mciSendStringW(cmd, out, cchOut, cb);
    if (e)
        set_err(a, e);
    return e;
}

/* 查询型命令：把返回的字符串转成整数 */
static int mci_query_int(Audio *a, int *out, const wchar_t *fmt, ...)
{
    wchar_t cmd[MCI_CMD_MAX];
    wchar_t buf[128];
    va_list ap;
    MCIERROR e;

    va_start(ap, fmt);
    _vsnwprintf(cmd, MCI_CMD_MAX - 1, fmt, ap);
    va_end(ap);
    cmd[MCI_CMD_MAX - 1] = 0;

    buf[0] = 0;
    e = mciSendStringW(cmd, buf, 128, NULL);
    if (e) {
        set_err(a, e);
        return 0;
    }
    if (out)
        *out = (int)_wtoi(buf);
    return 1;
}

Audio *audio_create(HWND notifyWnd)
{
    Audio *a = (Audio *)calloc(1, sizeof(Audio));
    if (!a)
        return NULL;

    /* appvol.c 用的 WASAPI 会话 API 需要 COM；
       SHBrowseForFolderW 也要，初始化一次两边都能用 */
    CoInitializeEx(NULL, COINIT_APARTMENTTHREADED);

    a->notify = notifyWnd;
    a->volume = 80;
    return a;
}

void audio_destroy(Audio *a)
{
    if (!a)
        return;
    audio_close(a);
    free(a);
    CoUninitialize();
}

void audio_close(Audio *a)
{
    if (!a)
        return;
    if (a->open) {
        mci_cmd(a, NULL, NULL, 0, L"close %ls", MCI_ALIAS);
        a->open = 0;
    }
    a->ended = 0;
}

int audio_open(Audio *a, const wchar_t *path)
{
    if (!a || !path)
        return 0;

    audio_close(a);

    /* 先让 MCI 按扩展名自己挑设备（mp3->MPEGVideo, wav->WaveAudio）；
       失败再显式指定 MPEGVideo。 */
    if (mci_cmd(a, NULL, NULL, 0, L"open \"%ls\" alias %ls",
                path, MCI_ALIAS) != 0) {
        if (mci_cmd(a, NULL, NULL, 0, L"open \"%ls\" type mpegvideo alias %ls",
                    path, MCI_ALIAS) != 0) {
            return 0;
        }
    }

    a->open = 1;
    a->ended = 0;
    audio_set_volume(a, a->volume);   /* 新设备要重新设一次音量 */
    return 1;
}

int audio_play(Audio *a)
{
    if (!a || !a->open)
        return 0;
    a->ended = 0;
    if (mci_cmd(a, a->notify, NULL, 0, L"play %ls notify", MCI_ALIAS) != 0)
        return 0;

    /* 播放开始后本进程的音频会话才存在，这时才能应用音量 */
    appvol_set(a->volume);
    return 1;
}

int audio_pause(Audio *a)
{
    if (!a || !a->open)
        return 0;
    return mci_cmd(a, NULL, NULL, 0, L"pause %ls", MCI_ALIAS) == 0;
}

int audio_resume(Audio *a)
{
    if (!a || !a->open)
        return 0;
    return mci_cmd(a, a->notify, NULL, 0, L"resume %ls notify", MCI_ALIAS) == 0;
}

int audio_stop(Audio *a)
{
    if (!a || !a->open)
        return 0;
    a->ended = 0;   /* 主动停止不算"自然播完" */
    return mci_cmd(a, NULL, NULL, 0, L"stop %ls", MCI_ALIAS) == 0;
}

int audio_seek(Audio *a, int ms)
{
    if (!a || !a->open)
        return 0;
    if (ms < 0)
        ms = 0;

    if (audio_is_playing(a)) {
        /* 播放中：用 play from 直接跳，否则画面会停在原地 */
        return mci_cmd(a, a->notify, NULL, 0,
                       L"play %ls from %d notify", MCI_ALIAS, ms) == 0;
    }
    return mci_cmd(a, NULL, NULL, 0, L"seek %ls to %d", MCI_ALIAS, ms) == 0;
}

int audio_set_volume(Audio *a, int vol)
{
    if (!a)
        return 0;

    if (vol < 0)   vol = 0;
    if (vol > 100) vol = 100;
    a->volume = vol;

    if (!a->open)
        return 1;   /* 记下来，等 open 后再应用 */

    /* 首选 MCI 自己的音量控制（0..1000）。
       实测本机 mciqtz32 驱动不支持，会返回"驱动程序无法识别指定的命令"，
       这时退回 WASAPI 的按进程会话音量。 */
    if (mci_cmd(a, NULL, NULL, 0, L"setaudio %ls volume to %d",
                MCI_ALIAS, vol * 10) == 0)
        return 1;

    a->errText[0] = 0;   /* 别把这个"意料之中"的失败显示到状态栏 */

    /* 会话还没建立（播放尚未开始）时返回 0，等 audio_play 里再补一次 */
    return appvol_set(vol);
}

int audio_get_volume(Audio *a)
{
    return a ? a->volume : 0;
}

int audio_get_length(Audio *a, int *ms)
{
    if (!a || !a->open)
        return 0;
    return mci_query_int(a, ms, L"status %ls length", MCI_ALIAS);
}

int audio_get_position(Audio *a, int *ms)
{
    if (!a || !a->open)
        return 0;
    return mci_query_int(a, ms, L"status %ls position", MCI_ALIAS);
}

int audio_is_playing(Audio *a)
{
    wchar_t cmd[128], buf[128];
    MCIERROR e;

    if (!a || !a->open)
        return 0;

    swprintf(cmd, 128, L"status %ls mode", MCI_ALIAS);
    buf[0] = 0;
    e = mciSendStringW(cmd, buf, 128, NULL);
    if (e) {
        set_err(a, e);
        return 0;
    }
    return wcsstr(buf, L"playing") != NULL;
}

int audio_is_open(Audio *a)
{
    return a && a->open;
}

int audio_handle_message(Audio *a, UINT msg, WPARAM wp, LPARAM lp)
{
    (void)lp;
    if (!a)
        return 0;

    if (msg == MM_MCINOTIFY) {
        /* wp: MCI_NOTIFY_SUCCESSFUL / _SUPERSEDED / _ABORTED / _FAILURE */
        if (wp == MCI_NOTIFY_SUCCESSFUL)
            a->ended = 1;
        return 1;   /* 一律吃掉，不让 DefWindowProc 处理 */
    }
    return 0;
}

int audio_take_ended(Audio *a)
{
    int r;
    if (!a)
        return 0;
    r = a->ended;
    a->ended = 0;
    return r;
}

const wchar_t *audio_last_error(Audio *a)
{
    if (!a)
        return L"";
    return a->errText;
}

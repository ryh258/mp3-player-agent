/* audio_mf.c —— 用 Windows Media Foundation 实现的音频后端（主用）
 *
 * 为什么不用 MCI：
 *   本机实测 MCI 的 MPEGVideo 设备（mciqtz32.dll）打不开任何 MP3 ——
 *   对有效文件返回 277 (MCIERR_INTERNAL)，对不存在的文件返回 275
 *   (MCIERR_FILE_NOT_FOUND)，说明驱动能加载、能找到文件，但构建 DirectShow
 *   解码图时内部失败（这台机器的 DirectShow MP3 解码器已不可用）。
 *   而 Media Foundation 的 MP3 解码器是注册好的，所以改走 MF。
 *
 * 结构：
 *   解码线程独占 IMFSourceReader（MF 对象要求同线程使用），
 *   解出 16 位 PCM 后喂给 waveOut 播放。
 *   主线程只通过几个 volatile 变量和事件跟它通信，不直接碰 MF 对象。
 *
 * 音量：仍然用 appvol.c 的 WASAPI 按进程会话音量
 *       （waveOut 播放同样会在本进程创建一个音频会话）。
 */
#include "audio.h"
#include "appvol.h"

#ifndef COBJMACROS
#define COBJMACROS          /* 让 mfreadwrite.h 提供 IMFSourceReader_* 便捷宏 */
#endif

#include <windows.h>
#include <mfapi.h>
#include <mfidl.h>
#include <mfreadwrite.h>
#include <process.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>

#define PATH_MAX_W  1024
#define NBUF        8              /* waveOut 缓冲块数 */
#define BUFSIZE     16384          /* 每块字节数（16 位 PCM 下约 93ms @44.1k 立体声） */

enum { CMD_NONE = 0, CMD_PLAY, CMD_PAUSE, CMD_STOP, CMD_SEEK, CMD_CLOSE };
enum { ST_STOPPED = 0, ST_PLAYING, ST_PAUSED };

/* 自己定义空 GUID，避免依赖 libuuid 里的 GUID_NULL */
static const GUID MY_GUID_NULL = { 0, 0, 0, { 0, 0, 0, 0, 0, 0, 0, 0 } };

struct Audio {
    HWND    notify;
    int     open;
    int     volume;            /* 0..100 */

    HANDLE  hThread;
    HANDLE  hWake;             /* auto-reset：唤醒解码线程 */
    HANDLE  hOpened;           /* manual-reset：打开流程结束 */

    volatile LONG quit;
    volatile LONG cmd;
    volatile LONG cmdSeekMs;
    volatile LONG state;
    volatile LONG ended;
    volatile LONG posMs;
    volatile LONG durMs;
    volatile LONG openResult;  /* 1 成功 / -1 失败 / 0 进行中 */

    wchar_t path[PATH_MAX_W];
    wchar_t errText[256];
    DWORD   lastErr;
    int     leaked;            /* 解码线程没能退出，对象不能释放 */
};

/* ------------------------------------------------------------------ */
/* 小工具                                                              */
/* ------------------------------------------------------------------ */

static void set_err(Audio *a, const wchar_t *what, HRESULT hr)
{
    a->lastErr = (DWORD)hr;
    swprintf(a->errText, 256, L"%ls (0x%08X)", what, (unsigned)hr);
}

static DWORD wo_pos_bytes(HWAVEOUT hwo)
{
    MMTIME t;
    if (!hwo)
        return 0;
    t.wType = TIME_BYTES;
    if (waveOutGetPosition(hwo, &t, sizeof(t)) != MMSYSERR_NOERROR)
        return 0;
    if (t.wType != TIME_BYTES)
        return 0;
    return t.u.cb;
}

/* 打开文件、设置输出为 16 位 PCM、取格式与时长。全部在解码线程里调用。
   每一步都单独报错，方便定位是哪一层出的问题。 */
static HRESULT open_reader(Audio *a, IMFSourceReader **ppReader, WAVEFORMATEX **ppWfx)
{
    IMFByteStream   *pBS    = NULL;
    IMFSourceReader *reader = NULL;
    IMFMediaType    *pType  = NULL;
    IMFMediaType    *pCur   = NULL;
    WAVEFORMATEX    *wfx    = NULL;
    UINT32           cbWfx  = 0;
    PROPVARIANT      var;
    HRESULT          hr;

    /* 用字节流方式打开，比 MFCreateSourceReaderFromURL 更直接可控 */
    hr = MFCreateFile(MF_ACCESSMODE_READ, MF_OPENMODE_FAIL_IF_NOT_EXIST,
                      MF_FILEFLAGS_NONE, a->path, &pBS);
    if (FAILED(hr)) {
        set_err(a, L"MFCreateFile 打不开文件", hr);
        return hr;
    }

    hr = MFCreateSourceReaderFromByteStream(pBS, NULL, &reader);
    pBS->lpVtbl->Release(pBS);
    if (FAILED(hr)) {
        set_err(a, L"MFCreateSourceReaderFromByteStream 失败", hr);
        return hr;
    }

    /* 只要求 PCM，采样率/声道交给 MF 自己决定（它会插入必要的解码器） */
    hr = MFCreateMediaType(&pType);
    if (FAILED(hr)) { set_err(a, L"MFCreateMediaType 失败", hr); goto done; }

    hr = pType->lpVtbl->SetGUID(pType, &MF_MT_MAJOR_TYPE, &MFMediaType_Audio);
    if (FAILED(hr)) { set_err(a, L"SetGUID(MAJOR)", hr); goto done; }

    hr = pType->lpVtbl->SetGUID(pType, &MF_MT_SUBTYPE, &MFAudioFormat_PCM);
    if (FAILED(hr)) { set_err(a, L"SetGUID(SUBTYPE)", hr); goto done; }

    hr = IMFSourceReader_SetCurrentMediaType(reader,
             MF_SOURCE_READER_FIRST_AUDIO_STREAM, NULL, pType);
    if (FAILED(hr)) { set_err(a, L"SetCurrentMediaType(PCM) 失败", hr); goto done; }

    hr = IMFSourceReader_GetCurrentMediaType(reader,
             MF_SOURCE_READER_FIRST_AUDIO_STREAM, &pCur);
    if (FAILED(hr)) { set_err(a, L"GetCurrentMediaType 失败", hr); goto done; }

    hr = MFCreateWaveFormatExFromMFMediaType(pCur, &wfx, &cbWfx,
                                             MFWaveFormatExConvertFlag_Normal);
    if (FAILED(hr)) { set_err(a, L"取 WAVEFORMATEX 失败", hr); goto done; }

    /* 时长（100ns 单位） */
    memset(&var, 0, sizeof(var));
    if (SUCCEEDED(IMFSourceReader_GetPresentationAttribute(
            reader, MF_SOURCE_READER_MEDIASOURCE, &MF_PD_DURATION, &var))) {
        if (var.vt == VT_UI8)
            a->durMs = (LONG)(var.uhVal.QuadPart / 10000);
    }

done:
    if (pCur)  pCur->lpVtbl->Release(pCur);
    if (pType) pType->lpVtbl->Release(pType);

    if (FAILED(hr)) {
        if (wfx)    CoTaskMemFree(wfx);
        if (reader) reader->lpVtbl->Release(reader);
        return hr;
    }
    *ppReader = reader;
    *ppWfx    = wfx;
    return S_OK;
}

/* ------------------------------------------------------------------ */
/* 解码线程                                                            */
/* ------------------------------------------------------------------ */

static unsigned __stdcall decode_thread(void *param)
{
    Audio          *a = (Audio *)param;
    IMFSourceReader *reader = NULL;
    WAVEFORMATEX   *wfx = NULL;
    HWAVEOUT        hwo = NULL;
    WAVEHDR         hdr[NBUF];
    BYTE           *buf[NBUF];
    int             haveHdr[NBUF];
    BYTE           *stage = NULL;          /* 暂存当前 sample 的 PCM */
    DWORD           stageCap = 0, stageLen = 0, stageOff = 0;
    DWORD           baseBytes = 0;
    LONG            baseMs = 0;
    int             eos = 0, i;
    HRESULT         hr;

    memset(hdr, 0, sizeof(hdr));
    memset(buf, 0, sizeof(buf));
    memset(haveHdr, 0, sizeof(haveHdr));

    CoInitializeEx(NULL, COINIT_MULTITHREADED);
    MFStartup(MF_VERSION, MFSTARTUP_LITE);

    hr = open_reader(a, &reader, &wfx);
    if (FAILED(hr)) {
        set_err(a, L"打开文件失败", hr);
        a->openResult = -1;
        SetEvent(a->hOpened);
        MFShutdown();
        CoUninitialize();
        return 0;
    }

    if (waveOutOpen(&hwo, WAVE_MAPPER, wfx, 0, 0, CALLBACK_NULL) != MMSYSERR_NOERROR) {
        set_err(a, L"waveOutOpen 失败", E_FAIL);
        a->openResult = -1;
        SetEvent(a->hOpened);
        reader->lpVtbl->Release(reader);
        CoTaskMemFree(wfx);
        MFShutdown();
        CoUninitialize();
        return 0;
    }

    for (i = 0; i < NBUF; i++)
        buf[i] = (BYTE *)malloc(BUFSIZE);

    a->posMs = 0;
    a->openResult = 1;
    SetEvent(a->hOpened);

    /* ---------------- 主循环 ---------------- */
    while (!a->quit) {
        LONG c = InterlockedExchange(&a->cmd, CMD_NONE);

        /* --- 处理命令 --- */
        if (c == CMD_CLOSE)
            break;

        if (c == CMD_SEEK) {
            LONG ms = a->cmdSeekMs;
            PROPVARIANT var;
            int wasPlaying = (a->state == ST_PLAYING);

            waveOutReset(hwo);
            for (i = 0; i < NBUF; i++) {
                if (haveHdr[i]) {
                    waveOutUnprepareHeader(hwo, &hdr[i], sizeof(WAVEHDR));
                    haveHdr[i] = 0;
                }
            }
            memset(&var, 0, sizeof(var));
            var.vt = VT_I8;
            var.hVal.QuadPart = (LONGLONG)ms * 10000LL;
            IMFSourceReader_SetCurrentPosition(reader, &MY_GUID_NULL, &var);
            IMFSourceReader_Flush(reader, MF_SOURCE_READER_FIRST_AUDIO_STREAM);

            baseMs = ms;
            baseBytes = wo_pos_bytes(hwo);
            stageLen = stageOff = 0;
            eos = 0;
            a->posMs = ms;
            a->state = wasPlaying ? ST_PLAYING : ST_PAUSED;
            continue;
        }

        if (c == CMD_STOP) {
            waveOutReset(hwo);
            for (i = 0; i < NBUF; i++) {
                if (haveHdr[i]) {
                    waveOutUnprepareHeader(hwo, &hdr[i], sizeof(WAVEHDR));
                    haveHdr[i] = 0;
                }
            }
            {
                PROPVARIANT var;
                memset(&var, 0, sizeof(var));
                var.vt = VT_I8;
                var.hVal.QuadPart = 0;
                IMFSourceReader_SetCurrentPosition(reader, &MY_GUID_NULL, &var);
                IMFSourceReader_Flush(reader, MF_SOURCE_READER_FIRST_AUDIO_STREAM);
            }
            baseMs = 0;
            baseBytes = wo_pos_bytes(hwo);
            stageLen = stageOff = 0;
            eos = 0;
            a->posMs = 0;
            a->state = ST_STOPPED;
            continue;
        }

        if (c == CMD_PLAY) {
            if (eos) {                       /* 已播完，从头再来 */
                PROPVARIANT var;
                waveOutReset(hwo);
                for (i = 0; i < NBUF; i++) {
                    if (haveHdr[i]) {
                        waveOutUnprepareHeader(hwo, &hdr[i], sizeof(WAVEHDR));
                        haveHdr[i] = 0;
                    }
                }
                memset(&var, 0, sizeof(var));
                var.vt = VT_I8;
                IMFSourceReader_SetCurrentPosition(reader, &MY_GUID_NULL, &var);
                IMFSourceReader_Flush(reader, MF_SOURCE_READER_FIRST_AUDIO_STREAM);
                baseMs = 0;
                baseBytes = wo_pos_bytes(hwo);
                stageLen = stageOff = 0;
                eos = 0;
                a->posMs = 0;
                a->state = ST_PLAYING;       /* 缓冲是空的，直接进播放态 */
                continue;
            }
            if (a->state == ST_PAUSED)
                waveOutRestart(hwo);
            a->state = ST_PLAYING;
            continue;
        }

        if (c == CMD_PAUSE) {
            waveOutPause(hwo);
            a->state = ST_PAUSED;
            continue;
        }

        /* --- 更新播放位置 --- */
        if (wfx && wfx->nAvgBytesPerSec > 0) {
            DWORD cur = wo_pos_bytes(hwo);
            LONG played = (LONG)(cur - baseBytes);
            if (played < 0) played = 0;
            a->posMs = baseMs + (LONG)((LONGLONG)played * 1000 /
                                       (LONGLONG)wfx->nAvgBytesPerSec);
        }

        if (a->state != ST_PLAYING) {
            WaitForSingleObject(a->hWake, 30);
            continue;
        }

        /* --- 回收播完的缓冲 --- */
        for (i = 0; i < NBUF; i++) {
            if (haveHdr[i] && (hdr[i].dwFlags & WHDR_DONE)) {
                waveOutUnprepareHeader(hwo, &hdr[i], sizeof(WAVEHDR));
                haveHdr[i] = 0;
            }
        }

        /* --- 播完了：等缓冲排空再报"曲终" --- */
        if (eos) {
            int any = 0;
            for (i = 0; i < NBUF; i++)
                if (haveHdr[i]) any = 1;
            if (!any) {
                a->ended = 1;
                a->state = ST_STOPPED;
            }
            WaitForSingleObject(a->hWake, 20);
            continue;
        }

        /* --- 找一个空闲缓冲 --- */
        {
            int k = -1, nbytes = 0;

            for (i = 0; i < NBUF; i++) {
                if (!haveHdr[i]) { k = i; break; }
            }
            if (k < 0) {
                WaitForSingleObject(a->hWake, 20);
                continue;
            }

            /* 暂存区空了就读下一个 sample */
            if (stageOff >= stageLen) {
                IMFSample *pSample = NULL;
                DWORD sflags = 0;

                stageLen = stageOff = 0;
                hr = IMFSourceReader_ReadSample(reader,
                        MF_SOURCE_READER_FIRST_AUDIO_STREAM, 0,
                        NULL, &sflags, NULL, &pSample);

                if (sflags & MF_SOURCE_READERF_ENDOFSTREAM)
                    eos = 1;

                if (pSample) {
                    IMFMediaBuffer *pMB = NULL;
                    if (SUCCEEDED(pSample->lpVtbl->ConvertToContiguousBuffer(pSample, &pMB)) && pMB) {
                        BYTE *pData = NULL;
                        DWORD cbMax = 0, cbCur = 0;
                        if (SUCCEEDED(pMB->lpVtbl->Lock(pMB, &pData, &cbMax, &cbCur)) && cbCur > 0) {
                            if (cbCur > stageCap) {          /* 暂存区按需扩容 */
                                BYTE *np = (BYTE *)realloc(stage, cbCur);
                                if (np) { stage = np; stageCap = cbCur; }
                            }
                            if (stage && stageCap >= cbCur) {
                                memcpy(stage, pData, cbCur);
                                stageLen = cbCur;
                            }
                            pMB->lpVtbl->Unlock(pMB);
                        }
                        pMB->lpVtbl->Release(pMB);
                    }
                    pSample->lpVtbl->Release(pSample);
                }

                if (eos) { WaitForSingleObject(a->hWake, 10); continue; }
                if (stageLen == 0) { WaitForSingleObject(a->hWake, 10); continue; }
            }

            /* 把暂存区的一段填进 waveOut 缓冲 */
            nbytes = (int)(stageLen - stageOff);
            if (nbytes > BUFSIZE) nbytes = BUFSIZE;

            memcpy(buf[k], stage + stageOff, (size_t)nbytes);
            stageOff += (DWORD)nbytes;

            memset(&hdr[k], 0, sizeof(WAVEHDR));
            hdr[k].lpData         = (LPSTR)buf[k];
            hdr[k].dwBufferLength = (DWORD)nbytes;
            if (waveOutPrepareHeader(hwo, &hdr[k], sizeof(WAVEHDR)) != MMSYSERR_NOERROR)
                continue;
            if (waveOutWrite(hwo, &hdr[k], sizeof(WAVEHDR)) != MMSYSERR_NOERROR) {
                waveOutUnprepareHeader(hwo, &hdr[k], sizeof(WAVEHDR));
                continue;
            }
            haveHdr[k] = 1;
        }
    }

    /* ---------------- 清理 ---------------- */
    a->state = ST_STOPPED;
    if (hwo) {
        waveOutReset(hwo);
        for (i = 0; i < NBUF; i++) {
            if (haveHdr[i])
                waveOutUnprepareHeader(hwo, &hdr[i], sizeof(WAVEHDR));
        }
        waveOutClose(hwo);
    }
    for (i = 0; i < NBUF; i++)
        free(buf[i]);
    free(stage);

    if (reader) reader->lpVtbl->Release(reader);
    if (wfx)    CoTaskMemFree(wfx);

    MFShutdown();
    CoUninitialize();
    return 0;
}

/* ------------------------------------------------------------------ */
/* 对外接口                                                            */
/* ------------------------------------------------------------------ */

Audio *audio_create(HWND notifyWnd)
{
    Audio *a = (Audio *)calloc(1, sizeof(Audio));
    if (!a)
        return NULL;

    CoInitializeEx(NULL, COINIT_APARTMENTTHREADED);   /* appvol / 文件对话框要用 */

    a->notify = notifyWnd;
    a->volume = 80;
    a->hWake   = CreateEventW(NULL, FALSE, FALSE, NULL);
    a->hOpened = CreateEventW(NULL, TRUE,  FALSE, NULL);
    return a;
}

void audio_close(Audio *a)
{
    if (!a)
        return;

    if (a->hThread) {
        InterlockedExchange(&a->quit, 1);
        InterlockedExchange(&a->cmd, CMD_CLOSE);
        if (a->hWake) SetEvent(a->hWake);
        if (WaitForSingleObject(a->hThread, 6000) == WAIT_OBJECT_0) {
            CloseHandle(a->hThread);
        } else {
            /* 线程卡在解码/驱动的阻塞调用里没退出来。
               这时既不能关句柄、更不能释放对象，否则线程会踩到已释放内存。
               宁可泄漏一点内存，也不要 use-after-free。 */
            a->leaked = 1;
        }
        a->hThread = NULL;
    }

    a->open    = 0;
    a->ended   = 0;
    a->state   = ST_STOPPED;
    a->posMs   = 0;
    a->durMs   = 0;
    a->openResult = 0;
}

void audio_destroy(Audio *a)
{
    if (!a)
        return;
    audio_close(a);
    if (a->leaked) {
        /* 线程还活着，放着别动，只把 COM 计数还回去 */
        CoUninitialize();
        return;
    }
    if (a->hWake)   CloseHandle(a->hWake);
    if (a->hOpened) CloseHandle(a->hOpened);
    free(a);
    CoUninitialize();
}

int audio_open(Audio *a, const wchar_t *path)
{
    if (!a || !path)
        return 0;

    /* 上一次的线程还卡着没退出来：绝不能再开一个，否则两个线程会同时
       操作同一个 Audio 结构。 */
    if (a->leaked) {
        wcsncpy(a->errText, L"上一次播放的线程未能正常结束，请重启程序", 256);
        a->errText[255] = 0;
        return 0;
    }

    audio_close(a);

    wcsncpy(a->path, path, PATH_MAX_W);
    a->path[PATH_MAX_W - 1] = 0;
    a->errText[0] = 0;
    a->lastErr = 0;
    a->quit = 0;
    a->cmd = CMD_NONE;
    a->cmdSeekMs = 0;
    a->state = ST_STOPPED;
    a->ended = 0;
    a->posMs = 0;
    a->durMs = 0;
    a->openResult = 0;

    ResetEvent(a->hOpened);

    a->hThread = (HANDLE)_beginthreadex(NULL, 0, decode_thread, a, 0, NULL);
    if (!a->hThread)
        return 0;

    /* 等解码线程把文件打开（最多 15 秒） */
    WaitForSingleObject(a->hOpened, 15000);

    if (a->openResult != 1) {
        audio_close(a);
        return 0;
    }

    a->open = 1;
    appvol_set(a->volume);      /* 会话可能还不存在，播放时会再设一次 */
    return 1;
}

int audio_play(Audio *a)
{
    if (!a || !a->open)
        return 0;
    a->ended = 0;
    InterlockedExchange(&a->cmd, CMD_PLAY);
    SetEvent(a->hWake);
    appvol_set(a->volume);      /* 播放开始后音频会话才建立 */
    return 1;
}

int audio_pause(Audio *a)
{
    if (!a || !a->open)
        return 0;
    InterlockedExchange(&a->cmd, CMD_PAUSE);
    SetEvent(a->hWake);
    return 1;
}

int audio_resume(Audio *a)
{
    if (!a || !a->open)
        return 0;
    a->ended = 0;
    InterlockedExchange(&a->cmd, CMD_PLAY);
    SetEvent(a->hWake);
    return 1;
}

int audio_stop(Audio *a)
{
    if (!a || !a->open)
        return 0;
    a->ended = 0;               /* 主动停止不算自然播完 */
    InterlockedExchange(&a->cmd, CMD_STOP);
    SetEvent(a->hWake);
    return 1;
}

int audio_seek(Audio *a, int ms)
{
    if (!a || !a->open)
        return 0;
    if (ms < 0)
        ms = 0;
    a->ended = 0;
    InterlockedExchange(&a->cmdSeekMs, ms);
    InterlockedExchange(&a->cmd, CMD_SEEK);
    SetEvent(a->hWake);
    return 1;
}

int audio_set_volume(Audio *a, int vol)
{
    if (!a)
        return 0;
    if (vol < 0)   vol = 0;
    if (vol > 100) vol = 100;
    a->volume = vol;
    if (!a->open)
        return 1;
    return appvol_set(vol);     /* 会话没建立时返回 0，播放后会再设一次 */
}

int audio_get_volume(Audio *a)
{
    return a ? a->volume : 0;
}

int audio_get_length(Audio *a, int *ms)
{
    if (!a || !a->open)
        return 0;
    if (ms) *ms = a->durMs;
    return a->durMs > 0;
}

int audio_get_position(Audio *a, int *ms)
{
    if (!a || !a->open)
        return 0;
    if (ms) *ms = a->posMs;
    return 1;
}

int audio_is_playing(Audio *a)
{
    return a && a->state == ST_PLAYING;
}

int audio_is_open(Audio *a)
{
    return a && a->open;
}

int audio_handle_message(Audio *a, UINT msg, WPARAM wp, LPARAM lp)
{
    /* MF 后端不需要窗口消息（曲终靠线程置标志，主循环轮询） */
    (void)a; (void)msg; (void)wp; (void)lp;
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

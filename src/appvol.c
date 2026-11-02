/* appvol.c —— 用 WASAPI 音频会话 API 调整本进程音量，并读取实时峰值
 *
 * 背景：MCI 的 setaudio volume 在 mciqtz32 上不支持；waveOutSetVolume 改的是
 *       全系统音量、会干扰别的程序。所以改用音频会话：
 *       找到属于本进程的会话，单独调它。
 *
 * 额外用途：GetPeakValue() 能读到本进程正在输出的音频峰值。
 *           自检用它来证明"真的在出声"，而不只是"位置在走"。
 */
#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0601
#endif

#include "appvol.h"

#include <windows.h>
/* initguid.h 必须放在这些头文件之前：它让头文件里的 DEFINE_GUID
   真正生成 GUID 定义。MinGW 的 libuuid 里没有这几个新版 GUID，
   所以只能自己生成（否则链接期报 undefined reference）。 */
#include <initguid.h>
#include <mmdeviceapi.h>
#include <audiopolicy.h>
#include <endpointvolume.h>
#include <stdlib.h>

/* MinGW 的 endpointvolume.h 只给了 IAudioMeterInformation 的前向声明，
   既没有结构体定义也没有 IID 声明（IID_ISimpleAudioVolume 在 audioclient.h 里，
   而这个的 IID 整个 include 目录都找不到）。这里按 Windows SDK 补上最小可用版本。
   只要不调用它，定义多余的方法不影响——虚表布局必须和系统的一致。 */
struct IAudioMeterInformationVtbl {
    HRESULT (STDMETHODCALLTYPE *QueryInterface)(IAudioMeterInformation *This,
                                                REFIID riid, void **ppvObject);
    ULONG   (STDMETHODCALLTYPE *AddRef)(IAudioMeterInformation *This);
    ULONG   (STDMETHODCALLTYPE *Release)(IAudioMeterInformation *This);
    HRESULT (STDMETHODCALLTYPE *GetPeakValue)(IAudioMeterInformation *This,
                                              float *pfPeak);
    HRESULT (STDMETHODCALLTYPE *GetMeteringChannelCount)(IAudioMeterInformation *This,
                                              UINT *pnChannelCount);
    HRESULT (STDMETHODCALLTYPE *GetChannelsPeakValues)(IAudioMeterInformation *This,
                                              UINT32 u32ChannelCount, float *afPeakValues);
    HRESULT (STDMETHODCALLTYPE *QueryHardwareSupport)(IAudioMeterInformation *This,
                                              DWORD *pdwHardwareSupportMask);
};

struct IAudioMeterInformation {
    struct IAudioMeterInformationVtbl *lpVtbl;
};

/* {C02216F6-8C67-4B5B-9D00-D008E73E0064} */
static const GUID IID_IAudioMeterInformation_ =
    { 0xc02216f6, 0x8c67, 0x4b5b,
      { 0x9d, 0x00, 0xd0, 0x08, 0xe7, 0x3e, 0x00, 0x64 } };

/* 找到属于本进程的音频会话控制接口。
   音频渲染器跑在我们自己进程里，所以 GetProcessId() ==
   GetCurrentProcessId() 的那个会话就是我们的。
   返回的对象需要调用方 Release，找不到返回 NULL。 */
static IAudioSessionControl2 *find_our_session(void)
{
    IMMDeviceEnumerator     *pEnum  = NULL;
    IMMDevice               *pDev   = NULL;
    IAudioSessionManager2   *pMgr   = NULL;
    IAudioSessionEnumerator *pSess  = NULL;
    IAudioSessionControl2   *pFound = NULL;
    DWORD myPid = GetCurrentProcessId();
    int count = 0, i;

    if (FAILED(CoCreateInstance(&CLSID_MMDeviceEnumerator, NULL, CLSCTX_ALL,
                                &IID_IMMDeviceEnumerator, (void **)&pEnum)))
        return NULL;

    if (FAILED(pEnum->lpVtbl->GetDefaultAudioEndpoint(pEnum, eRender,
                                                      eMultimedia, &pDev)))
        goto done;

    if (FAILED(pDev->lpVtbl->Activate(pDev, &IID_IAudioSessionManager2,
                                      CLSCTX_ALL, NULL, (void **)&pMgr)))
        goto done;

    if (FAILED(pMgr->lpVtbl->GetSessionEnumerator(pMgr, &pSess)))
        goto done;

    if (FAILED(pSess->lpVtbl->GetCount(pSess, &count)))
        goto done;

    for (i = 0; i < count; i++) {
        IAudioSessionControl *pCtrl = NULL;
        DWORD pid = 0;

        if (FAILED(pSess->lpVtbl->GetSession(pSess, i, &pCtrl)))
            continue;

        if (SUCCEEDED(pCtrl->lpVtbl->QueryInterface(
                pCtrl, &IID_IAudioSessionControl2, (void **)&pFound))) {
            if (SUCCEEDED(pFound->lpVtbl->GetProcessId(pFound, &pid)) &&
                pid == myPid) {
                pCtrl->lpVtbl->Release(pCtrl);
                break;                      /* 找到自己的会话 */
            }
            pFound->lpVtbl->Release(pFound);
            pFound = NULL;
        }
        pCtrl->lpVtbl->Release(pCtrl);
    }

done:
    if (pSess) pSess->lpVtbl->Release(pSess);
    if (pMgr)  pMgr->lpVtbl->Release(pMgr);
    if (pDev)  pDev->lpVtbl->Release(pDev);
    if (pEnum) pEnum->lpVtbl->Release(pEnum);
    return pFound;
}

int appvol_set(int vol)
{
    IAudioSessionControl2 *pCtrl;
    ISimpleAudioVolume    *pVol = NULL;
    HRESULT hr;

    if (vol < 0)   vol = 0;
    if (vol > 100) vol = 100;

    pCtrl = find_our_session();
    if (!pCtrl)
        return 0;

    hr = pCtrl->lpVtbl->QueryInterface(pCtrl, &IID_ISimpleAudioVolume,
                                       (void **)&pVol);
    pCtrl->lpVtbl->Release(pCtrl);
    if (FAILED(hr) || !pVol)
        return 0;

    hr = pVol->lpVtbl->SetMasterVolume(pVol, (float)vol / 100.0f, NULL);
    pVol->lpVtbl->Release(pVol);
    return SUCCEEDED(hr);
}

int appvol_get(int *vol)
{
    IAudioSessionControl2 *pCtrl;
    ISimpleAudioVolume    *pVol = NULL;
    float f = 1.0f;
    HRESULT hr;

    pCtrl = find_our_session();
    if (!pCtrl)
        return 0;

    hr = pCtrl->lpVtbl->QueryInterface(pCtrl, &IID_ISimpleAudioVolume,
                                       (void **)&pVol);
    pCtrl->lpVtbl->Release(pCtrl);
    if (FAILED(hr) || !pVol)
        return 0;

    hr = pVol->lpVtbl->GetMasterVolume(pVol, &f);
    pVol->lpVtbl->Release(pVol);
    if (FAILED(hr))
        return 0;

    if (vol)
        *vol = (int)(f * 100.0f + 0.5f);
    return 1;
}

/* 读取本进程当前的音频输出峰值（0.0~1.0）。
   返回 1 成功，0 表示没有会话。这是"真的在出声"的硬证据。 */
int appvol_peak(float *peak)
{
    IAudioSessionControl2  *pCtrl;
    IAudioMeterInformation *pMeter = NULL;
    float p = 0.0f;
    HRESULT hr;

    if (peak)
        *peak = 0.0f;

    pCtrl = find_our_session();
    if (!pCtrl)
        return 0;

    hr = pCtrl->lpVtbl->QueryInterface(pCtrl, &IID_IAudioMeterInformation_,
                                       (void **)&pMeter);
    pCtrl->lpVtbl->Release(pCtrl);
    if (FAILED(hr) || !pMeter)
        return 0;

    hr = pMeter->lpVtbl->GetPeakValue(pMeter, &p);
    pMeter->lpVtbl->Release(pMeter);
    if (FAILED(hr))
        return 0;

    if (peak)
        *peak = p;
    return 1;
}

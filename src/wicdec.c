/* wicdec.c —— 用 WIC 解码图片
 *
 * 为什么换掉 OleLoadPicture：
 *   实测同一台机器上，OleLoadPicture 对同样的 JPEG 数据有时成功、有时全部失败
 *   （用户机器上 7 个文件全失败，开发机上全成功），而且补 COM 初始化也救不回来。
 *   OleLoadPicture 是 1990 年代的老接口，对 OLE 单元状态很敏感。
 *   WIC 是 Vista 起的现代解码框架，不挑单元状态，并且原生支持 PNG。
 *
 * 关键技巧：
 *   输出用 GUID_WICPixelFormat32bppBGR（不是 ...BGRA）——WIC 会把 alpha 通道
 *   填成 255。这样得到的 DIB 直接就能塞进 ILC_COLOR32 的 ImageList，
 *   不会再出现"StretchBlt 不写 alpha 导致整张图透明"的坑。
 */
#include "wicdec.h"

#include <windows.h>
#include <wincodec.h>
#include <shlwapi.h>
#include <objbase.h>
#include <stdlib.h>
#include <string.h>

static void safe_release(IUnknown *p)
{
    if (p)
        p->lpVtbl->Release(p);
}

HBITMAP wic_render(const void *data, int len,
                   int outW, int outH, int cropSquare, COLORREF padColor)
{
    IWICImagingFactory     *fac    = NULL;
    IStream                *stream = NULL;
    IWICBitmapDecoder      *dec    = NULL;
    IWICBitmapFrameDecode  *frame  = NULL;
    IWICFormatConverter    *conv   = NULL;
    IWICBitmapClipper      *clip   = NULL;
    IWICBitmapScaler       *scale  = NULL;
    IWICBitmapSource       *src    = NULL;
    HBITMAP    dst = NULL;
    BYTE      *tmp = NULL;
    void      *bits = NULL;
    UINT       sw = 0, sh = 0;
    int        dw, dh, dx, dy;
    double     k;
    HRESULT    hr;
    int        ok = 0;

    if (!data || len <= 0 || outW < 8 || outH < 8)
        return NULL;

    /* COM 可能还没初始化（不同单元模式都试一遍） */
    hr = CoInitializeEx(NULL, COINIT_APARTMENTTHREADED);
    if (FAILED(hr))
        CoInitializeEx(NULL, COINIT_MULTITHREADED);

    hr = CoCreateInstance(&CLSID_WICImagingFactory, NULL, CLSCTX_INPROC_SERVER,
                          &IID_IWICImagingFactory, (void **)&fac);
    if (FAILED(hr) || !fac)
        return NULL;

    /* 内存 -> IStream（SHCreateMemStream 会自己拷贝一份数据） */
    stream = SHCreateMemStream((const BYTE *)data, (UINT)len);
    if (!stream) {
        safe_release((IUnknown *)fac);
        return NULL;
    }

    hr = fac->lpVtbl->CreateDecoderFromStream(fac, stream, NULL,
                                              WICDecodeMetadataCacheOnDemand, &dec);
    if (FAILED(hr) || !dec)
        goto done;

    hr = dec->lpVtbl->GetFrame(dec, 0, &frame);
    if (FAILED(hr) || !frame)
        goto done;

    /* 统一转成 32bppBGR（alpha 由 WIC 填 255） */
    hr = fac->lpVtbl->CreateFormatConverter(fac, &conv);
    if (FAILED(hr) || !conv)
        goto done;
    hr = conv->lpVtbl->Initialize(conv, (IWICBitmapSource *)frame,
                                  &GUID_WICPixelFormat32bppBGR,
                                  WICBitmapDitherTypeNone, NULL, 0.0,
                                  WICBitmapPaletteTypeCustom);
    if (FAILED(hr))
        goto done;
    src = (IWICBitmapSource *)conv;

    hr = src->lpVtbl->GetSize(src, &sw, &sh);
    if (FAILED(hr) || sw == 0 || sh == 0)
        goto done;

    /* 算目标区域 */
    if (cropSquare) {
        UINT side = (sw < sh) ? sw : sh;
        WICRect r;
        r.X = (INT)((sw - side) / 2);
        r.Y = (INT)((sh - side) / 2);
        r.Width  = (INT)side;
        r.Height = (INT)side;

        hr = fac->lpVtbl->CreateBitmapClipper(fac, &clip);
        if (FAILED(hr) || !clip)
            goto done;
        hr = clip->lpVtbl->Initialize(clip, src, &r);
        if (FAILED(hr))
            goto done;
        src = (IWICBitmapSource *)clip;

        dw = outW;
        dh = outH;
    } else {
        k = (double)outW / sw;
        if ((double)outH / sh < k)
            k = (double)outH / sh;
        dw = (int)(sw * k + 0.5);
        dh = (int)(sh * k + 0.5);
        if (dw < 1) dw = 1;
        if (dh < 1) dh = 1;
    }
    dx = (outW - dw) / 2;
    dy = (outH - dh) / 2;

    /* 缩放 */
    hr = fac->lpVtbl->CreateBitmapScaler(fac, &scale);
    if (FAILED(hr) || !scale)
        goto done;
    hr = scale->lpVtbl->Initialize(scale, src, (UINT)dw, (UINT)dh,
                                   WICBitmapInterpolationModeFant);
    if (FAILED(hr))
        goto done;

    /* 目标 DIB（自上而下 32bpp） */
    {
        BITMAPINFO bi;
        HDC screen = GetDC(NULL);
        ZeroMemory(&bi, sizeof(bi));
        bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
        bi.bmiHeader.biWidth = outW;
        bi.bmiHeader.biHeight = -outH;
        bi.bmiHeader.biPlanes = 1;
        bi.bmiHeader.biBitCount = 32;
        bi.bmiHeader.biCompression = BI_RGB;
        dst = CreateDIBSection(screen ? screen : NULL, &bi, DIB_RGB_COLORS, &bits, NULL, 0);
        if (screen)
            ReleaseDC(NULL, screen);
        if (!dst || !bits) {
            if (dst) { DeleteObject(dst); dst = NULL; }
            goto done;
        }
    }

    /* 先铺底色 */
    {
        int i, n = outW * outH;
        unsigned char *q = (unsigned char *)bits;
        for (i = 0; i < n; i++) {
            q[i * 4 + 0] = GetBValue(padColor);
            q[i * 4 + 1] = GetGValue(padColor);
            q[i * 4 + 2] = GetRValue(padColor);
            q[i * 4 + 3] = 255;
        }
    }

    /* 解码到临时缓冲，再逐行贴进 DIB（CopyPixels 的 stride 是连续的） */
    tmp = (BYTE *)malloc((size_t)dw * dh * 4);
    if (!tmp)
        goto done;

    hr = scale->lpVtbl->CopyPixels(scale, NULL, (UINT)(dw * 4),
                                   (UINT)(dw * dh * 4), tmp);
    if (FAILED(hr))
        goto done;

    {
        int y;
        unsigned char *q = (unsigned char *)bits;
        for (y = 0; y < dh; y++) {
            unsigned char *drow = q + ((size_t)(y + dy) * outW + dx) * 4;
            memcpy(drow, tmp + (size_t)y * dw * 4, (size_t)dw * 4);
            {   /* 兜底：确保 alpha 是 255 */
                int x;
                for (x = 0; x < dw; x++)
                    drow[x * 4 + 3] = 255;
            }
        }
    }

    ok = 1;

done:
    if (tmp) free(tmp);
    safe_release((IUnknown *)scale);
    safe_release((IUnknown *)clip);
    safe_release((IUnknown *)conv);
    safe_release((IUnknown *)frame);
    safe_release((IUnknown *)dec);
    if (stream) stream->lpVtbl->Release(stream);
    safe_release((IUnknown *)fac);

    if (!ok && dst) {           /* 中途失败：别返回半成品，让上层走兜底图 */
        DeleteObject(dst);
        dst = NULL;
    }
    return dst;
}

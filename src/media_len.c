/* media_len.c —— 纯文件解析估算音频时长（不开解码器）
 *
 * MP3：从 ID3v2 标签之后逐帧走，累加每帧采样数再除以采样率。
 *      CBR / VBR 都准，代价是要顺序读一遍文件（对几 MB 的歌曲是毫秒级）。
 *      文件太大（>64MB）时退化为按首帧比特率估算。
 * WAV：直接读 RIFF/fmt/data 头算。
 */
#include "media_len.h"
#include "util.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MAX_SCAN_BYTES (64u * 1024u * 1024u)

/* ---- 同步安全整数（ID3v2 头） ---- */
static unsigned id3_size(const unsigned char *p)
{
    return ((unsigned)(p[0] & 0x7F) << 21) |
           ((unsigned)(p[1] & 0x7F) << 14) |
           ((unsigned)(p[2] & 0x7F) << 7)  |
           ((unsigned)(p[3] & 0x7F));
}

/* ---- MPEG 音频帧头解析 ---- */
typedef struct Mp3Frame {
    int  valid;
    int  layer;        /* 1/2/3 */
    int  mpeg1;        /* 是否 MPEG-1 */
    int  bitrate;      /* bps */
    int  samplerate;
    int  padding;
    int  samples;      /* 本帧采样数 */
    int  length;       /* 本帧字节数 */
} Mp3Frame;

static const int BITRATE_V1[3][16] = {
    { 0,32,64,96,128,160,192,224,256,288,320,352,384,416,448,0 },   /* Layer1 */
    { 0,32,48,56,64,80,96,112,128,160,192,224,256,320,384,0 },      /* Layer2 */
    { 0,32,40,48,56,64,80,96,112,128,160,192,224,256,320,0 }        /* Layer3 */
};
static const int BITRATE_V2[3][16] = {
    { 0,32,48,56,64,80,96,112,128,144,160,176,192,224,256,0 },      /* Layer1 */
    { 0,8,16,24,32,40,48,56,64,80,96,112,128,144,160,0 },           /* Layer2 */
    { 0,8,16,24,32,40,48,56,64,80,96,112,128,144,160,0 }            /* Layer3 */
};
static const int SRATE_V1[3] = { 44100, 48000, 32000 };
static const int SRATE_V2[3] = { 22050, 24000, 16000 };
static const int SRATE_V25[3] = { 11025, 12000, 8000 };

static Mp3Frame parse_frame(const unsigned char *p)
{
    Mp3Frame f;
    int ver, layerIdx, brIdx, srIdx;

    memset(&f, 0, sizeof(f));
    if (p[0] != 0xFF || (p[1] & 0xE0) != 0xE0)
        return f;

    ver      = (p[1] >> 3) & 0x03;   /* 0=MPEG2.5 1=保留 2=MPEG2 3=MPEG1 */
    layerIdx = (p[1] >> 1) & 0x03;   /* 1=Layer3 2=Layer2 3=Layer1 */
    brIdx    = (p[2] >> 4) & 0x0F;
    srIdx    = (p[2] >> 2) & 0x03;
    f.padding = (p[2] >> 1) & 0x01;

    if (ver == 1 || layerIdx == 0 || brIdx == 0 || brIdx == 15 || srIdx == 3)
        return f;                     /* 非法组合 */

    f.mpeg1 = (ver == 3);
    f.layer = 4 - layerIdx;           /* 1/2/3 */

    f.bitrate = (f.mpeg1 ? BITRATE_V1 : BITRATE_V2)[f.layer - 1][brIdx] * 1000;
    if (f.bitrate <= 0)
        return f;
    f.samplerate = (ver == 3) ? SRATE_V1[srIdx] :
                   (ver == 2) ? SRATE_V2[srIdx] : SRATE_V25[srIdx];

    if (f.layer == 1)
        f.samples = 384;
    else if (f.layer == 2)
        f.samples = 1152;
    else
        f.samples = f.mpeg1 ? 1152 : 576;

    /* 帧长（字节）= 每帧采样数/8 * 比特率 / 采样率 + 填充
       Layer1 以 4 字节为槽位，所以再乘 4 */
    if (f.layer == 1)
        f.length = (int)((12 * f.bitrate / f.samplerate + f.padding) * 4);
    else
        f.length = (int)(f.samples / 8 * f.bitrate / f.samplerate + f.padding);

    if (f.length < 4)
        return f;

    f.valid = 1;
    return f;
}

static int mp3_duration(const wchar_t *path, FILE *f, long audioStart)
{
    unsigned char *buf;
    long fileLen;
    unsigned long long totalSamples = 0;
    int samplerate = 0;
    long pos, len;

    (void)path;

    fseek(f, 0, SEEK_END);
    fileLen = ftell(f);
    if (fileLen <= audioStart + 4)
        return -1;

    len = fileLen - audioStart;
    if ((unsigned long)len > MAX_SCAN_BYTES) {
        /* 太大：按首帧比特率粗略估算 */
        unsigned char hdr[8];
        Mp3Frame fr;
        fseek(f, audioStart, SEEK_SET);
        if (fread(hdr, 1, sizeof(hdr), f) != sizeof(hdr))
            return -1;
        fr = parse_frame(hdr);
        if (!fr.valid)
            return -1;
        return (int)((double)len * 8.0 * 1000.0 / (double)fr.bitrate);
    }

    buf = (unsigned char *)malloc((size_t)len);
    if (!buf)
        return -1;
    fseek(f, audioStart, SEEK_SET);
    if ((long)fread(buf, 1, (size_t)len, f) != len) {
        free(buf);
        return -1;
    }

    pos = 0;
    while (pos + 4 <= len) {
        Mp3Frame fr = parse_frame(buf + pos);
        if (!fr.valid) {
            pos++;                    /* 对不上就往前挪一格重新同步 */
            continue;
        }
        if (samplerate == 0)
            samplerate = fr.samplerate;
        totalSamples += (unsigned long long)fr.samples;
        pos += fr.length;
    }

    free(buf);

    if (samplerate <= 0 || totalSamples == 0)
        return -1;
    return (int)(totalSamples * 1000ULL / (unsigned long long)samplerate);
}

/* ---- WAV：RIFF 头 ---- */
static int wav_duration(FILE *f)
{
    unsigned char h[12];
    unsigned int  byteRate = 0, dataSize = 0;

    fseek(f, 0, SEEK_SET);
    if (fread(h, 1, 12, f) != 12)
        return -1;
    if (memcmp(h, "RIFF", 4) != 0 || memcmp(h + 8, "WAVE", 4) != 0)
        return -1;

    for (;;) {
        unsigned char ch[8];
        unsigned int sz;

        if (fread(ch, 1, 8, f) != 8)
            break;
        sz = (unsigned)ch[4] | ((unsigned)ch[5] << 8) |
             ((unsigned)ch[6] << 16) | ((unsigned)ch[7] << 24);

        if (memcmp(ch, "fmt ", 4) == 0) {
            unsigned char fmt[16];
            if (sz < 16 || fread(fmt, 1, 16, f) != 16)
                return -1;
            byteRate = (unsigned)fmt[8] | ((unsigned)fmt[9] << 8) |
                       ((unsigned)fmt[10] << 16) | ((unsigned)fmt[11] << 24);
            fseek(f, (long)(sz - 16), SEEK_CUR);
        } else if (memcmp(ch, "data", 4) == 0) {
            dataSize = sz;
            break;
        } else {
            fseek(f, (long)((sz + 1) & ~1u), SEEK_CUR);   /* 块按偶数字节对齐 */
        }
    }

    if (byteRate == 0 || dataSize == 0)
        return -1;
    return (int)((unsigned long long)dataSize * 1000ULL / byteRate);
}

int media_length_ms(const wchar_t *path)
{
    FILE *f;
    const wchar_t *ext;
    unsigned char head[10];
    long audioStart = 0;
    int r = -1;

    if (!path)
        return -1;

    f = _wfopen(path, L"rb");
    if (!f)
        return -1;

    ext = util_ext(path);

    if (_wcsicmp(ext, L".wav") == 0) {
        r = wav_duration(f);
    } else if (_wcsicmp(ext, L".mp3") == 0 || _wcsicmp(ext, L".mp2") == 0 ||
               _wcsicmp(ext, L".mpa") == 0) {
        /* 跳过 ID3v2 标签 */
        if (fread(head, 1, 10, f) == 10 && memcmp(head, "ID3", 3) == 0) {
            int major = head[3];
            if (major >= 2 && major <= 4)
                audioStart = 10 + (long)id3_size(head + 6);
        }
        r = mp3_duration(path, f, audioStart);
    }

    fclose(f);
    return r;
}

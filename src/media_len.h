/* media_len.h —— 不打开解码器就估算音频总时长 */
#ifndef MEDIA_LEN_H
#define MEDIA_LEN_H

#include <windows.h>

/* 返回时长（毫秒）；无法判断返回 -1。
 *
 * 为什么要自己算：时长本来是在播放时从 Media Foundation 拿的，
 * 结果列表里没播过的曲目只能显示 "--:--"，看着像坏了。
 * 这里用纯文件解析先给出时长：
 *   WAV  -> 解析 RIFF 头
 *   MP3  -> 从 ID3v2 之后逐帧走，累加采样数（CBR/VBR 都准）
 * 其它格式返回 -1，等真正播放时再补上。 */
int media_length_ms(const wchar_t *path);

#endif /* MEDIA_LEN_H */

/* appvol.h —— 按进程调整音量
 *
 * 背景：MCI 的 mciqtz32 驱动不支持 setaudio volume（实测返回
 *       "驱动程序无法识别指定的命令"）。而 waveOutSetVolume 改的是全系统
 *       音量，会干扰别的程序，不能用。
 *
 * 做法：用 WASAPI 的音频会话 API（IAudioSessionManager2 / ISimpleAudioVolume），
 *       找到属于本进程的音频会话，单独调本程序的音量。
 *       这正是 Beta 换 Media Foundation 后要用的同一套 API。
 */
#ifndef APPVOL_H
#define APPVOL_H

/* 设置本进程音量 vol: 0..100。返回 1 成功；0 = 还没找到本进程的音频会话
   （通常在开始播放之前），调用方应在播放开始后再试一次。 */
int appvol_set(int vol);

/* 读取本进程音量（0..100）；返回 1 成功，0 没有会话 */
int appvol_get(int *vol);

/* 读取本进程当前的音频输出峰值（0.0~1.0）。
   返回 1 成功，0 表示找不到会话。用于验证"真的在出声"。 */
int appvol_peak(float *peak);

#endif /* APPVOL_H */

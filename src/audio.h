/* audio.h —— 音频后端抽象接口
 *
 * 上层（UI / 播放器逻辑）只调用这里声明的函数，完全不知道底下是 MCI 还是
 * Media Foundation。Alpha 的实现是 audio_mci.c；Beta 想换成 MF，只需新增
 * audio_mf.c 实现同一组接口，把 build.bat 里的 audio_mci.c 换掉即可。
 */
#ifndef AUDIO_H
#define AUDIO_H

#include <windows.h>

typedef struct Audio Audio;

/* 创建后端实例；notifyWnd 用于接收"曲终"通知（可传 NULL，则靠轮询） */
Audio *audio_create(HWND notifyWnd);
void   audio_destroy(Audio *a);

/* 打开一个音频文件（会先关掉上一个）。返回 1 成功 / 0 失败 */
int  audio_open(Audio *a, const wchar_t *path);
void audio_close(Audio *a);

int  audio_play(Audio *a);
int  audio_pause(Audio *a);
int  audio_resume(Audio *a);
int  audio_stop(Audio *a);

/* 跳转到指定毫秒；播放中会边播边跳，暂停/停止时只移动光标 */
int  audio_seek(Audio *a, int ms);

/* 音量 0..100 */
int  audio_set_volume(Audio *a, int vol);
int  audio_get_volume(Audio *a);

/* 取总时长 / 当前位置（毫秒） */
int  audio_get_length(Audio *a, int *ms);
int  audio_get_position(Audio *a, int *ms);

int  audio_is_open(Audio *a);
int  audio_is_playing(Audio *a);

/* 消息钩子：主窗口过程在 switch 之前调用它。
 * 返回 1 表示这条消息已被后端消费，窗口过程不要再处理。 */
int  audio_handle_message(Audio *a, UINT msg, WPARAM wp, LPARAM lp);

/* 取"自然播放结束"事件：返回 1 并把标志清掉（每次播完只报一次） */
int  audio_take_ended(Audio *a);

/* 最近一次失败的原因（用于状态栏提示） */
const wchar_t *audio_last_error(Audio *a);

#endif /* AUDIO_H */

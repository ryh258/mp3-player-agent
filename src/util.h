/* util.h —— 通用小工具（时间格式化、路径、扩展名判断） */
#ifndef UTIL_H
#define UTIL_H

#include <windows.h>
#include <stddef.h>

/* 把毫秒格式化成 mm:ss 或 h:mm:ss；ms < 0 输出 "--:--" */
void util_format_time(int ms, wchar_t *buf, size_t cch);

/* 取文件名部分（返回 path 内部的指针，不要 free） */
const wchar_t *util_basename(const wchar_t *path);

/* 取扩展名（含点，返回 path 内部指针；无扩展名返回空串） */
const wchar_t *util_ext(const wchar_t *path);

/* 是否是我们支持的音频扩展名（不区分大小写） */
int util_is_audio_file(const wchar_t *path);

/* 由完整路径生成显示用标题：去目录、去扩展名 */
void util_title_from_path(const wchar_t *path, wchar_t *out, size_t cch);

#endif /* UTIL_H */

/* crash.c —— 崩溃时留证据
 *
 * 背景：开发过程中出现过一次"程序在曲子播完后偶发消失"，但复现不了。
 *       没有日志就只能猜。这里装一个未处理异常过滤器，崩溃时把
 *       异常码、出错地址、调用栈（模块名 + 偏移）写进 crash.log。
 *
 * 用 exe + addr2line 可以把偏移还原成源码行：
 *   addr2line -e MP3Player.exe -f -C 0x1234
 */
#include "crash.h"

#include <windows.h>
#include <stdio.h>
#include <string.h>
#include <wchar.h>

#define CRASH_STACK_MAX 40

static LONG WINAPI crash_filter(EXCEPTION_POINTERS *ep)
{
    FILE *f;
    void *frames[CRASH_STACK_MAX];
    USHORT n, i;
    wchar_t path[MAX_PATH];

    GetModuleFileNameW(NULL, path, MAX_PATH);
    {
        wchar_t *p = wcsrchr(path, L'\\');
        if (p) {
            wcscpy(p + 1, L"crash.log");
        }
    }

    f = _wfopen(path, L"a, ccs=UTF-8");
    if (!f)
        return EXCEPTION_EXECUTE_HANDLER;

    fwprintf(f, L"===== 崩溃记录 =====\n");
    fwprintf(f, L"异常代码 : 0x%08X\n", (unsigned)ep->ExceptionRecord->ExceptionCode);
    fwprintf(f, L"出错地址 : %p\n", ep->ExceptionRecord->ExceptionAddress);
    fwprintf(f, L"异常标志 : 0x%08X\n", (unsigned)ep->ExceptionRecord->ExceptionFlags);

    if (ep->ExceptionRecord->NumberParameters >= 2 &&
        (ep->ExceptionRecord->ExceptionCode == EXCEPTION_ACCESS_VIOLATION ||
         ep->ExceptionRecord->ExceptionCode == EXCEPTION_IN_PAGE_ERROR)) {
        fwprintf(f, L"访问类型 : %ls\n",
                 ep->ExceptionRecord->ExceptionInformation[0] ? L"写入" : L"读取");
        fwprintf(f, L"访问地址 : %p\n",
                 (void *)ep->ExceptionRecord->ExceptionInformation[1]);
    }

    n = (USHORT)CaptureStackBackTrace(0, CRASH_STACK_MAX, frames, NULL);
    fwprintf(f, L"调用栈 (%u 层):\n", (unsigned)n);
    for (i = 0; i < n; i++) {
        HMODULE mod = NULL;
        wchar_t name[MAX_PATH];

        name[0] = 0;
        if (GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                               GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                               (LPCWSTR)frames[i], &mod) && mod) {
            wchar_t *p;
            GetModuleFileNameW(mod, name, MAX_PATH);
            p = wcsrchr(name, L'\\');
            if (p)
                memmove(name, p + 1, (wcslen(p) + 1) * sizeof(wchar_t));
            fwprintf(f, L"  [%02u] %ls + 0x%llX\n", (unsigned)i, name,
                     (unsigned long long)((BYTE *)frames[i] - (BYTE *)mod));
        } else {
            fwprintf(f, L"  [%02u] %p\n", (unsigned)i, frames[i]);
        }
    }
    fwprintf(f, L"\n");
    fflush(f);
    fclose(f);

    return EXCEPTION_EXECUTE_HANDLER;
}

void crash_install(void)
{
    SetUnhandledExceptionFilter(crash_filter);
}

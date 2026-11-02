/* crash.h —— 未处理异常记录 */
#ifndef CRASH_H
#define CRASH_H

/* 安装未处理异常过滤器：崩溃时把异常码、地址、调用栈写进 exe 同目录的 crash.log。
   目的是不让程序"莫名其妙就没了"——尤其是那种偶发、难以复现的崩溃。 */
void crash_install(void);

#endif /* CRASH_H */

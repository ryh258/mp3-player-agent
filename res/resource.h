/* resource.h —— 只给资源脚本（app.rc）和 C 代码共用的资源 ID
 *
 * 单独放一份的原因：windres 解析 app.rc 时不能牵扯进 app.h 里那一堆 C 头文件。
 */
#ifndef RESOURCE_H
#define RESOURCE_H

/* 图标 */
#define IDI_APPICON        101
#define IDR_SPLASH         200

/* 输入对话框 */
#define IDD_INPUT          102
#define IDC_INPUT_PROMPT   1001
#define IDC_INPUT_EDIT     1002

#endif /* RESOURCE_H */

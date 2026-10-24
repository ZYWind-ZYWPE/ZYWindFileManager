/* BUG-1 修复验证：地址栏 Enter
 * ------------------------------------------------------------------
 * 上一轮 check\entertest 已实测坐实：单行 EDIT 按回车**不会**给父窗口发任何
 * WM_COMMAND（通知码只有 0x0100/0x0200/0x0300/0x0400…），VK_RETURN 也被 EDIT
 * 自己吃掉 —— 所以 WM_COMMAND 里 "HIWORD(wp)==0" 是死分支。
 *
 * 本程序验证修复方案（子类化 EDIT，在子类里拦 WM_KEYDOWN + VK_RETURN）确实能
 * 收到回车，且父窗口依旧收不到任何 WM_COMMAND（即这条路径只能靠子类化）。
 *
 * 编译：cl /nologo /std:c++17 /utf-8 /EHsc /W4 /MT /DUNICODE /D_UNICODE editsub.cpp user32.lib
 */
#include <windows.h>
#include <stdio.h>

static WNDPROC g_old     = NULL;
static int     g_enter   = 0;   /* 子类拦到的回车次数 */
static int     g_parentCmd = 0; /* 父窗口收到的 WM_COMMAND 次数 */

static LRESULT CALLBACK EditSubProc(HWND h, UINT m, WPARAM wp, LPARAM lp)
{
    if (m == WM_KEYDOWN && wp == VK_RETURN) {
        g_enter++;
        printf("  [subclass] caught VK_RETURN  -> would call FmGo()\n");
        return 0;               /* 已处理：回车不会插进编辑框，也不冒泡 */
    }
    return CallWindowProcW(g_old, h, m, wp, lp);
}

static LRESULT CALLBACK HostProc(HWND h, UINT m, WPARAM wp, LPARAM lp)
{
    if (m == WM_COMMAND) {
        g_parentCmd++;
        printf("  [parent]   WM_COMMAND id=%d notify=0x%04X\n", LOWORD(wp), HIWORD(wp));
    }
    if (m == WM_DESTROY) PostQuitMessage(0);
    return DefWindowProcW(h, m, wp, lp);
}

int wmain()
{
    WNDCLASSW wc = { 0 };
    wc.lpfnWndProc = HostProc;
    wc.hInstance   = GetModuleHandleW(NULL);
    wc.lpszClassName = L"EdSubHost";
    RegisterClassW(&wc);

    HWND host = CreateWindowExW(0, L"EdSubHost", L"t", 0, 0, 0, 400, 300,
                                NULL, NULL, wc.hInstance, NULL);
    HWND he = CreateWindowExW(0, L"EDIT", L"", WS_CHILD | ES_AUTOHSCROLL,
                              0, 0, 300, 24, host, (HMENU)1001, wc.hInstance, NULL);
    g_old = (WNDPROC)SetWindowLongPtrW(he, GWLP_WNDPROC, (LONG_PTR)EditSubProc);

    printf("--- send VK_RETURN twice ---\n");
    SendMessageW(he, WM_KEYDOWN, VK_RETURN, 0);
    SendMessageW(he, WM_KEYDOWN, VK_RETURN, 0);

    printf("--- result ---\n");
    printf("subclass caught : %d   (expect 2)\n", g_enter);
    printf("parent WM_COMMAND: %d  (expect 0 -> 证明只能靠子类化)\n", g_parentCmd);
    printf("%s\n", (g_enter == 2 && g_parentCmd == 0) ? "RESULT: PASS" : "RESULT: FAIL");
    return 0;
}

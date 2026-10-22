/* 小实验：普通窗口里的单行 EDIT，按回车到底会不会给父窗口发 WM_COMMAND？
 * 用来判定 files.cpp 里 "HIWORD(wp)==0 视为地址栏回车" 这段是不是死代码。
 * 结果写到同目录 entertest.log。 */
#include <windows.h>
#include <stdio.h>

static HWND g_edit;
static FILE* g_log;

static LRESULT CALLBACK Wp(HWND h, UINT m, WPARAM wp, LPARAM lp)
{
    switch (m) {
    case WM_COMMAND:
        fprintf(g_log, "WM_COMMAND id=%d notify=0x%04X\n", (int)LOWORD(wp), (int)HIWORD(wp));
        fflush(g_log);
        break;
    case WM_KEYDOWN:
        if (wp == VK_RETURN) {
            fprintf(g_log, "parent got WM_KEYDOWN VK_RETURN (focus=%d)\n",
                    GetFocus() == g_edit ? 1 : 0);
            fflush(g_log);
        }
        break;
    case WM_DESTROY: PostQuitMessage(0); return 0;
    }
    return DefWindowProcW(h, m, wp, lp);
}

int main()
{
    g_log = fopen("entertest.log", "w");
    HINSTANCE hi = GetModuleHandleW(NULL);
    WNDCLASSW wc = { 0 };
    wc.lpfnWndProc = Wp;
    wc.hInstance = hi;
    wc.lpszClassName = L"EntTestW";
    RegisterClassW(&wc);
    HWND h = CreateWindowExW(0, L"EntTestW", L"t", 0, 0, 0, 200, 100, NULL, NULL, hi, NULL);
    g_edit = CreateWindowExW(0, L"EDIT", L"", WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL,
                             0, 0, 180, 24, h, (HMENU)1001, hi, NULL);
    SetFocus(g_edit);

    /* 模拟真实按键：回车 */
    SendMessageW(g_edit, WM_KEYDOWN, VK_RETURN, 0x001C0001);
    SendMessageW(g_edit, WM_KEYUP, VK_RETURN, 0xC01C0001);
    /* 模拟输入字符 a（用于对照 EN_CHANGE / EN_UPDATE 的通知码） */
    SendMessageW(g_edit, WM_CHAR, L'a', 0x00010001);
    SendMessageW(g_edit, WM_KEYDOWN, VK_RETURN, 0x001C0001);
    SendMessageW(g_edit, WM_KEYUP, VK_RETURN, 0xC01C0001);

    MSG msg;
    while (PeekMessageW(&msg, NULL, 0, 0, PM_REMOVE)) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    fprintf(g_log, "--- end ---\n");
    fclose(g_log);
    return 0;
}

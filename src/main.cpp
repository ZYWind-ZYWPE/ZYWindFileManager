/******************************************************************************
 *  main.cpp - 竹影清风PE文件资源管理器  独立程序入口
 *  竹影清风制作   FirTeam出品
 *
 *  ────────────────────────────────────────────────────────────────────────
 *  本工程从「竹影清风PE核心 (ZYWPECore)」中提取「文件资源管理器」模块
 *  （Shell 核心）独立而成。
 *
 *  src\files.cpp 是文件管理器本体（已重绘为 iced 版工具合集的纯白扁平风格）；
 *  src\util.cpp / zywpe.h 已按「只留文件管理器用得到的代码」裁剪：
 *
 *      util.cpp  去掉了 DrawGradient / DrawRound / DrawTextXY / DrawBamboo /
 *                GetIconFrom / LoadPicture / EnablePriv / SplitStr / Dbg /
 *                ShowDesktop / DoShutdown / DoLogoff / TextToWide 及一批
 *                桌面/任务栏全局变量
 *      art.cpp   已整体移除（标题栏图标去掉、左栏盘符改用系统图标列表）
 *      zywpe.h   去掉桌面/任务栏/开始菜单/托盘/工具发现/PE定制/启动流程/
 *                命令解析/关于窗口 的全部声明与常量
 *      theme.h   新增：与 iced 版「竹影清风PE工具合集」一一对应的配色/尺寸
 *
 *  本文件是独立工程**新写**的入口，只做两件事：
 *    1. 提供 WinMain：初始化 → Fm_Open() → 消息循环 → 清理
 *    2. 补上原本由 boot.cpp 提供的 LogWrite() / GetLogPath()
 *       （util.cpp 内部有一处 LogWrite 调用，原工程里实现在 boot.cpp）
 *  ────────────────────────────────────────────────────────────────────────
 ******************************************************************************/
#include "zywpe.h"

/* ==========================================================================
 *  一、日志  ——  原工程由 boot.cpp 提供，此处独立实现
 *      日志文件写到 %TEMP%\ZYWFileManager.log，UTF-8 带 BOM，追加写入。
 *      （原 util.cpp 里的 Dbg() 是 LogWrite 的唯一调用方，随 Dbg 一并裁掉了；
 *        这里保留 LogWrite 是因为入口本身要用它记启动/退出/报错。）
 * ========================================================================== */
static std::wstring g_logPath;

std::wstring GetLogPath()
{
    if (!g_logPath.empty()) return g_logPath;

    wchar_t tmp[MAX_PATH] = { 0 };
    DWORD n = GetTempPathW(MAX_PATH, tmp);
    if (n && n < MAX_PATH && tmp[0]) {
        g_logPath = std::wstring(tmp) + L"ZYWFileManager.log";
    } else {
        wchar_t wd[MAX_PATH] = { 0 };
        GetWindowsDirectoryW(wd, MAX_PATH);
        g_logPath = std::wstring(wd[0] ? wd : L"C:\\Windows") + L"\\ZYWFileManager.log";
    }
    return g_logPath;
}

void LogWrite(const wchar_t* level, const wchar_t* text)
{
    const std::wstring path = GetLogPath();

    /* 文件不存在 → 先写 UTF-8 BOM，保证记事本/VS 打开不乱码 */
    bool needBom = (GetFileAttributesW(path.c_str()) == INVALID_FILE_ATTRIBUTES);

    FILE* f = NULL;
    if (_wfopen_s(&f, path.c_str(), L"ab") != 0 || !f) return;

    if (needBom) {
        const char bom[3] = { (char)0xEF, (char)0xBB, (char)0xBF };
        fwrite(bom, 1, 3, f);
    }

    SYSTEMTIME st;
    GetLocalTime(&st);
    std::wstring line = StrFormat(L"[%04d-%02d-%02d %02d:%02d:%02d] [%s] %s\r\n",
                                  st.wYear, st.wMonth, st.wDay,
                                  st.wHour, st.wMinute, st.wSecond,
                                  level, text);

    int len = WideCharToMultiByte(CP_UTF8, 0, line.c_str(), -1, NULL, 0, NULL, NULL);
    if (len > 1) {
        std::string a;
        a.resize(len);
        WideCharToMultiByte(CP_UTF8, 0, line.c_str(), -1, &a[0], len, NULL, NULL);
        fwrite(a.c_str(), 1, (size_t)len - 1, f);
    }
    fclose(f);
}

/* ==========================================================================
 *  二、命令行参数
 *      ZYWFileManager.exe                      → 打开「此电脑」
 *      ZYWFileManager.exe "D:\"                → 打开 D 盘
 *      ZYWFileManager.exe "D:\Tools"           → 打开该目录
 *      ZYWFileManager.exe "D:\Tools\a.txt"     → 打开该文件所在目录
 * ========================================================================== */
static std::wstring ParseTargetDir()
{
    int argc = 0;
    LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    if (!argv) return L"";

    std::wstring target;
    for (int i = 1; i < argc; i++) {
        std::wstring a = argv[i] ? argv[i] : L"";
        a = TrimStr(a);
        if (!a.empty()) { target = a; break; }   /* 只取第一个参数 */
    }
    LocalFree(argv);

    if (target.empty()) return L"";

    /* 正斜杠统一成反斜杠，并去掉末尾多余的 '\' */
    for (size_t i = 0; i < target.size(); i++)
        if (target[i] == L'/') target[i] = L'\\';
    while (target.size() > 3 && target.back() == L'\\') target.pop_back();

    /* 传进来的是「文件」而不是目录 → 退到它所在的目录 */
    DWORD attr = GetFileAttributesW(target.c_str());
    if (attr != INVALID_FILE_ATTRIBUTES && !(attr & FILE_ATTRIBUTE_DIRECTORY)) {
        size_t p = target.rfind(L'\\');
        target = (p == std::wstring::npos) ? L"" : target.substr(0, p);
        while (target.size() > 3 && target.back() == L'\\') target.pop_back();
    }

    return target;
}

/* ==========================================================================
 *  三、程序入口
 * ========================================================================== */
int WINAPI WinMain(HINSTANCE hInst, HINSTANCE, LPSTR, int)
{
    g_hInst = hInst;

    SetProcessDPIAware();                        /* 系统 DPI 感知：自己负责缩放 */
    InitDpi();                                   /* ★ 必须在上一行之后：算 g_dpi / g_scale */
    CoInitializeEx(NULL, COINIT_APARTMENTTHREADED);

    INITCOMMONCONTROLSEX ic = { sizeof(ic), ICC_WIN95_CLASSES };
    InitCommonControlsEx(&ic);

    InitFonts();                                 /* util.cpp：建字体句柄 */

    LogWrite(L"INFO", (std::wstring(L"=== 竹影清风PE文件资源管理器 ") + APP_VERSTR +
                       L" ===  log=" + GetLogPath()).c_str());
    /* 环境自检：PE 里排查「字发虚 / 中文变方框 / 界面太大太小」全看这几行。
     * 缩放 = 系统 DPI 缩放 × 密度系数（UI_DENSITY，可用 ZYW_UI_SCALE 覆盖）。 */
    LogWrite(L"INFO", StrFormat(L"环境: DPI=%d  密度=%.2f  缩放=%.3f  小图标=%dx%d  字体=%s",
                                g_dpi, g_density, g_scale,
                                SysSmallIconSize(), SysSmallIconSize(),
                                g_fontFace.c_str()).c_str());

    /* ---------- 打开窗口 ---------- */
    std::wstring target = ParseTargetDir();
    LogWrite(L"INFO", (L"target = " + (target.empty() ? std::wstring(L"(此电脑)") : target)).c_str());

    if (!Fm_Open(target.empty() ? NULL : target.c_str())) {
        LogWrite(L"ERR ", L"Fm_Open failed");
        const std::wstring err = L"文件资源管理器窗口创建失败。\n\n详见日志：\n" + GetLogPath();
        MsgBox(err.c_str());
        FreeFonts();
        CoUninitialize();
        return 1;
    }

    LogWrite(L"INFO", L"window ready");

    /* ---------- 消息循环 ----------
     * 注意：files.cpp 的 WM_DESTROY 里**没有** PostQuitMessage（原工程是
     * Shell，桌面/任务栏窗口常驻，不需要退出），所以这里用 Fm_Count() 判断：
     * 最后一个资源管理器窗口关闭后即退出进程。                              */
    MSG msg;
    while (Fm_Count() > 0 && GetMessageW(&msg, NULL, 0, 0) > 0) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    LogWrite(L"INFO", L"exit");

    Fm_CloseAll();                               /* 兜底：清掉残留窗口 */
    FreeFonts();
    CoUninitialize();
    return 0;
}

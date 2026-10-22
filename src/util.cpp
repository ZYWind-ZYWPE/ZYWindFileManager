/******************************************************************************
 *  util.cpp - 基础绘制 / 字体 / 路径 / 进程 工具
 *  竹影清风PE文件资源管理器   竹影清风制作  FirTeam出品
 *
 *  ────────────────────────────────────────────────────────────────────────
 *  本文件从「竹影清风PE核心 (ZYWPECore)」的 util.cpp **裁剪**而来。
 *
 *  已移除（文件管理器用不到）：
 *      DrawGradient（改纯白扁平风格后不再需要渐变）
 *      DrawRound   DrawTextXY   DrawBamboo（竹叶图标）
 *      GetIconFrom LoadPicture（WIC 图片解码）
 *      EnablePriv  SplitStr     Dbg
 *      ShowDesktop DoShutdown   DoLogoff（关机/注销）
 *      TextToWide（ANSI→宽字符，给读 .ini 命令脚本用的）
 *      以及 g_hDesk / g_hTaskbar / g_hStartMenu / g_hAbout / g_preview /
 *      g_running / g_fontBig / g_wallPath / g_wallMode / g_lastMsg 等
 *      只服务于桌面、任务栏、开始菜单、托盘的全局变量。
 *
 *  保留的每一个函数都在 files.cpp / main.cpp 里被实际引用（已逐个实测）。
 * ────────────────────────────────────────────────────────────────────────
 ******************************************************************************/
#include "zywpe.h"
#include <stdio.h>
#include <stdlib.h>          /* wcstod：读 ZYW_UI_SCALE 环境变量 */

/* ============================ 全局变量 ============================ */
HINSTANCE   g_hInst     = NULL;
HFONT       g_fontUI    = NULL;
HFONT       g_fontBold  = NULL;
HFONT       g_fontSmall = NULL;
std::wstring g_fontFace  = L"(未选)";        /* 实际选中的字体名，写日志用 */

/* ============================ DPI ============================ */
int   g_dpi   = 96;
float g_scale = 1.0f;
float g_density = UI_DENSITY;              /* 实际生效的密度系数（可被环境变量覆盖） */

/* 必须在 SetProcessDPIAware() 之后调用；否则拿到的是虚拟化后的 96 */
void InitDpi()
{
    HDC dc = GetDC(NULL);
    int d = dc ? GetDeviceCaps(dc, LOGPIXELSY) : 96;
    if (dc) ReleaseDC(NULL, dc);
    if (d < 96) d = 96;                 /* 低于 100% 不缩，免得字太小看不清 */
    g_dpi = d;

    /* 系统 DPI 只说明「屏幕有多密」，不代表界面该有多大。
     * 150% 的机器上按 1.50 原样放大，整套界面会偏大一圈，
     * 所以再乘一个密度系数把整体收一收（默认见 UI_DENSITY）。
     * 需要微调不必重编译：设环境变量 ZYW_UI_SCALE=0.85 / 1.0 即可。 */
    g_density = UI_DENSITY;
    wchar_t env[64] = { 0 };
    DWORD n = GetEnvironmentVariableW(L"ZYW_UI_SCALE", env, 64);
    if (n > 0 && n < 64) {
        wchar_t* end = NULL;
        double v = wcstod(env, &end);
        if (end && end != env && v >= 0.5 && v <= 2.0)
            g_density = (float)v;
    }

    g_scale = ((float)d / 96.0f) * g_density;
    if (g_scale < 0.6f) g_scale = 0.6f;   /* 兜底：再小就没法看了 */
    if (g_scale > 3.0f) g_scale = 3.0f;
}

/* 系统小图标边长：100% 时 16，150% 时 24。Shell 的图像列表就是这个尺寸，
 * 界面里所有图标必须按它摆放，写死 16 会导致图标偏下且溢出。 */
int SysSmallIconSize()
{
    int cx = GetSystemMetrics(SM_CXSMICON);
    return cx > 0 ? cx : 16;
}

/* ============================ 字体 ============================ */
static BOOL CALLBACK FontFindProc(const LOGFONTW* lf, const TEXTMETRICW*,
                                  DWORD, LPARAM lp)
{
    *(bool*)lp = true;
    return FALSE;
}

/* 这个字体能不能画出汉字？—— WinPE 的字体集是裁剪过的，很可能只有
 * 一个英文字体。选错字体中文会全变成方框，所以必须实测字形。 */
static bool FaceHasHanzi(const std::wstring& face)
{
    HDC dc = GetDC(NULL);
    if (!dc) return true;                       /* 测不了就当能用，别误伤 */
    HFONT f = CreateFontW(-12, 0, 0, 0, FW_NORMAL, 0, 0, 0, DEFAULT_CHARSET,
                          OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, DEFAULT_QUALITY,
                          DEFAULT_PITCH, face.c_str());
    HFONT of = (HFONT)SelectObject(dc, f);
    wchar_t ch = L'\u4E2D';                     /* 「中」 */
    WORD idx = 0;
    BOOL ok = GetGlyphIndicesW(dc, &ch, 1, &idx, GGI_MARK_NONEXISTING_GLYPHS);
    bool has = (ok != 0) && (idx != 0xFFFF);
    SelectObject(dc, of);
    DeleteObject(f);
    ReleaseDC(NULL, dc);
    return has;
}

static bool FaceExists(HDC dc, const wchar_t* name)
{
    bool found = false;
    LOGFONTW lf = { 0 };
    lf.lfCharSet = DEFAULT_CHARSET;
    wcscpy_s(lf.lfFaceName, name);
    EnumFontFamiliesExW(dc, &lf, (FONTENUMPROCW)FontFindProc, (LPARAM)&found, 0);
    return found;
}

static std::wstring ChooseFace()
{
    const wchar_t* cands[] = {
        L"Microsoft YaHei UI", L"Microsoft YaHei", L"\u5FAE\u8F6F\u96C5\u9ED1",
        L"SimHei", L"\u9ED1\u4F53", L"SimSun", L"\u5B8B\u4F53",
        L"MS Shell Dlg 2", L"Tahoma"
    };
    const int n = (int)(sizeof(cands) / sizeof(cands[0]));
    HDC dc = GetDC(NULL);
    if (!dc) return L"MS Shell Dlg 2";

    /* 第一轮：字体存在 **且** 能画汉字 */
    for (int i = 0; i < n; i++)
        if (FaceExists(dc, cands[i]) && FaceHasHanzi(cands[i])) {
            ReleaseDC(NULL, dc);
            return cands[i];
        }
    /* 第二轮：放宽到「只要存在」——总比默认字体强 */
    for (int i = 0; i < n; i++)
        if (FaceExists(dc, cands[i])) {
            ReleaseDC(NULL, dc);
            return cands[i];
        }
    ReleaseDC(NULL, dc);
    return L"MS Shell Dlg 2";
}

static HFONT MakeFont(int h, bool bold, const std::wstring& face)
{
    LOGFONTW lf = { 0 };
    lf.lfHeight   = -h;
    lf.lfWeight   = bold ? FW_BOLD : FW_NORMAL;
    lf.lfCharSet  = DEFAULT_CHARSET;
    /* ★ 必须用 CLEARTYPE_QUALITY：ANTIALIASED_QUALITY 是灰度抗锯齿，
     *   会把系统的 ClearType 覆盖掉，小字号下笔画发灰发虚。 */
    lf.lfQuality  = CLEARTYPE_QUALITY;
    wcscpy_s(lf.lfFaceName, face.c_str());
    return CreateFontIndirectW(&lf);
}

/* 字号按 96 DPI 的逻辑像素书写，S() 换算成物理像素（150% 时 16 → 24）。 */
void InitFonts()
{
    std::wstring face = ChooseFace();
    g_fontFace  = face;
    g_fontUI    = MakeFont(S(16), false, face);
    g_fontBold  = MakeFont(S(16), true,  face);
    g_fontSmall = MakeFont(S(13), false, face);
}

void FreeFonts()
{
    if (g_fontUI)    DeleteObject(g_fontUI);
    if (g_fontBold)  DeleteObject(g_fontBold);
    if (g_fontSmall) DeleteObject(g_fontSmall);
}

/* ============================ 绘制 ============================ */
/*  界面已改为 iced 版工具合集的纯白扁平风格，全部是纯色填充，不再需要渐变。 */
void DrawTextR(HDC hdc, const RECT& rc, const wchar_t* txt, HFONT f, COLORREF c, UINT fmt)
{
    HFONT old = (HFONT)SelectObject(hdc, f ? f : g_fontUI);
    SetBkMode(hdc, TRANSPARENT);
    SetTextColor(hdc, c);
    RECT r = rc;
    DrawTextW(hdc, txt, -1, &r, fmt);
    SelectObject(hdc, old);
}

std::wstring StrFormat(const wchar_t* fmt, ...)
{
    wchar_t buf[2048];
    va_list ap;
    va_start(ap, fmt);
    _vsnwprintf_s(buf, 2048, _TRUNCATE, fmt, ap);
    va_end(ap);
    return std::wstring(buf);
}

/* ============================ 路径 / 字符串 ============================ */
bool FileExistsW(const wchar_t* p)
{
    if (!p || !*p) return false;
    DWORD a = GetFileAttributesW(p);
    return (a != INVALID_FILE_ATTRIBUTES) && !(a & FILE_ATTRIBUTE_DIRECTORY);
}

std::wstring GetSysDir()
{
    wchar_t buf[MAX_PATH];
    GetSystemDirectoryW(buf, MAX_PATH);
    return std::wstring(buf);
}

std::wstring ExpandEnvStr(const std::wstring& s)
{
    wchar_t buf[4096];
    DWORD n = ExpandEnvironmentStringsW(s.c_str(), buf, 4096);
    if (n && n < 4096) return std::wstring(buf);
    return s;
}

std::wstring TrimStr(const std::wstring& s)
{
    size_t a = s.find_first_not_of(L" \t\r\n");
    if (a == std::wstring::npos) return L"";
    size_t b = s.find_last_not_of(L" \t\r\n");
    return s.substr(a, b - a + 1);
}

/* ============================ 进程 ============================ */
/* 执行一条命令：先按进程方式，失败再按 Shell 打开 */
bool RunCommand(const std::wstring& cmd, bool wait, bool hide, DWORD* exitCode)
{
    if (exitCode) *exitCode = 0;
    if (cmd.empty()) return false;
    std::wstring line = ExpandEnvStr(cmd);

    STARTUPINFOW si = { sizeof(si) };
    PROCESS_INFORMATION pi = { 0 };
    si.dwFlags = STARTF_USESHOWWINDOW;
    si.wShowWindow = hide ? SW_HIDE : SW_SHOWNORMAL;
    std::wstring mutableLine = line;
    DWORD flags = 0;
    BOOL ok = CreateProcessW(NULL, &mutableLine[0], NULL, NULL, FALSE, flags,
                             NULL, NULL, &si, &pi);
    if (ok) {
        if (wait) {
            WaitForSingleObject(pi.hProcess, INFINITE);
            if (exitCode) GetExitCodeProcess(pi.hProcess, exitCode);
        }
        CloseHandle(pi.hThread);
        CloseHandle(pi.hProcess);
        return true;
    }

    /* Shell 方式：分离路径与参数 */
    std::wstring path = line, args = L"";
    bool inQ = false;
    for (size_t i = 0; i < line.size(); i++) {
        if (line[i] == L'"') inQ = !inQ;
        if (line[i] == L' ' && !inQ) { path = line.substr(0, i); args = line.substr(i + 1); break; }
    }
    path = TrimStr(path);
    if (!path.empty() && path.front() == L'"' && path.back() == L'"')
        path = path.substr(1, path.size() - 2);

    SHELLEXECUTEINFOW sei = { sizeof(sei) };
    sei.fMask = SEE_MASK_NOASYNC | (wait ? SEE_MASK_NOCLOSEPROCESS : 0);
    sei.lpFile = path.c_str();
    sei.lpParameters = args.empty() ? NULL : args.c_str();
    sei.nShow = hide ? SW_HIDE : SW_SHOWNORMAL;
    if (!ShellExecuteExW(&sei)) return false;
    if (wait && sei.hProcess) {
        WaitForSingleObject(sei.hProcess, INFINITE);
        if (exitCode) GetExitCodeProcess(sei.hProcess, exitCode);
        CloseHandle(sei.hProcess);
    }
    return true;
}

/* ============================ 提示框 ============================ */
void MsgBox(const wchar_t* txt, const wchar_t* title)
{
    MessageBoxW(NULL, txt, title ? title : APP_TITLE, MB_OK | MB_TOPMOST | MB_SETFOREGROUND);
}

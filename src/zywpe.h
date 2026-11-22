/******************************************************************************
 *  zywpe.h - 竹影清风PE文件资源管理器  公共头文件
 *  竹影清风制作   FirTeam出品
 *
 *  ────────────────────────────────────────────────────────────────────────
 *  本文件从「竹影清风PE核心 (ZYWPECore)」的 zywpe.h **裁剪**而来。
 *
 *  只保留「文件资源管理器（Shell 核心）」真正用得到的声明；以下模块的
 *  全部声明与常量已移除（它们属于 ZYWPECore 外壳的其他部分，与本程序无关）：
 *
 *     桌面 desktop.cpp        任务栏 taskbar.cpp      开始菜单 startmenu.cpp
 *     托盘 tray.cpp           工具发现 toolfind.cpp   PE 定制 petools.cpp
 *     启动流程 boot.cpp       命令解析 commands.cpp   关于窗口 about.cpp
 *     线条图标 art.cpp（40 个图标全部移除：标题栏图标已去掉、左栏盘符改用
 *                     系统图标列表 SHGetFileInfoW，不再需要自绘图标）
 *
 *  裁剪依据：对 files.cpp / main.cpp 的实际引用逐个实测，不是凭名字猜的。
 * ────────────────────────────────────────────────────────────────────────
 ******************************************************************************/
#pragma once
#define WIN32_LEAN_AND_MEAN
#ifndef UNICODE
#define UNICODE
#define _UNICODE
#endif
#include <windows.h>
#include <windowsx.h>
#include <commctrl.h>
#include <shlobj.h>
#include <shellapi.h>
#include <shlwapi.h>
#include <objbase.h>
#include <string>
#include <vector>

/* ============================ 产品信息 ============================ */
#define APP_TITLE       L"竹影清风PE核心"
#define APP_VERSTR      L"1.0.0.4"

/* 程序图标资源 ID（res\app.rc 里 IDI_APP ICON "app.ico"；
 * app.ico 提取自系统 explorer.exe 的图标组 —— 就是「文件资源管理器」那个图标） */
#define IDI_APP         1

/* 本程序不带图标资源（exe 无 IDI_ICON1，窗口类也不设 hIcon）。 */

/* ============================ 配色 ============================ */
/* 全部配色集中在 theme.h（T_* 常量），与 iced 版「竹影清风PE工具合集」
 * 一一对应。这里不再保留任何本文件专属的颜色常量。 */

/* ============================ 全局 ============================ */
extern HINSTANCE   g_hInst;                  /* 实例句柄            */
extern HFONT       g_fontUI;                 /* 常规（列表/工具栏） */
extern HFONT       g_fontBold;               /* 加粗（列头/选中）   */
extern HFONT       g_fontSmall;              /* 小字（大小/日期）   */
extern std::wstring g_fontFace;              /* 实际选中的字体名（诊断用） */

/* ==================== DPI 缩放 (util.cpp) ==================== */
/*  程序是「系统 DPI 感知」的，所有坐标都是物理像素。
 *  界面尺寸一律按 96 DPI 的「逻辑像素」书写，用 S() 换算成物理像素，
 *  这样在 125% / 150% / 200% 缩放下观感一致、字体也不会发虚。
 *
 *  用法：InitDpi() 必须在 SetProcessDPIAware() **之后**调用一次。 */
extern int         g_dpi;                    /* 系统 DPI，96 = 100%  */
extern float       g_scale;                  /* 系统DPI缩放 × 密度系数 */
extern float       g_density;                /* 实际生效的密度系数    */
#define S(v)  ((int)((v) * g_scale + 0.5f))  /* 逻辑像素 → 物理像素  */

/* 界面密度系数：系统 DPI 只说屏幕有多密，不代表界面该有多大。
 * 150% 的机器上按 1.50 原样放大会偏大一圈，乘 0.90 把整体收一收。
 * 运行时可被环境变量 ZYW_UI_SCALE 覆盖（0.5 ~ 2.0），便于不重编译微调。 */
#define UI_DENSITY  0.90f

void     InitDpi();
int      SysSmallIconSize();                 /* 系统小图标边长 16/20/24… */

/* ==================== 工具函数 (util.cpp) ==================== */
void     InitFonts();
void     FreeFonts();
void     DrawTextR(HDC hdc, const RECT& rc, const wchar_t* txt, HFONT f, COLORREF c,
                   UINT fmt = DT_LEFT | DT_SINGLELINE | DT_VCENTER | DT_END_ELLIPSIS);
std::wstring StrFormat(const wchar_t* fmt, ...);
bool     RunCommand(const std::wstring& cmd, bool wait = false, bool hide = false,
                    DWORD* exitCode = NULL);
bool     FileExistsW(const wchar_t* p);
std::wstring GetSysDir();
std::wstring ExpandEnvStr(const std::wstring& s);
std::wstring TrimStr(const std::wstring& s);
void     MsgBox(const wchar_t* txt, const wchar_t* title = APP_TITLE);

/* ==================== 日志 (main.cpp) ==================== */
/*  原工程里 LogWrite / GetLogPath 实现在 boot.cpp，本工程独立实现在 main.cpp */
std::wstring GetLogPath();
void     LogWrite(const wchar_t* level, const wchar_t* text);

/* ==================== 资源管理器 (files.cpp) ===================== */
/*  本程序的主体 —— WinPE 中 explorer.exe 不存在，本模块即 Shell 核心。
 *  完全独立实现：工具栏 / 地址栏 / 左栏盘符 / 右栏列表 / 右键菜单 /
 *  复制剪切粘贴删除 / 重命名 / 新建文件夹 / 排序 / 多选 / 快捷键。      */
bool Fm_Open(const wchar_t* path);          /* 打开(复用或新建)，path=NULL 打开此电脑 */
void Fm_CloseAll();                          /* 关闭全部资源管理器窗口 */
int  Fm_Count();                             /* 当前窗口数 */
int  ZywInputBox(HWND owner, const wchar_t* title, const wchar_t* prompt,
                 std::wstring& value);       /* 自绘输入框，返回 0=确定 1=取消 */

/******************************************************************************
 *  files.cpp - 资源管理器（Shell 核心：类似 explorer 的文件管理工具）
 *  竹影清风PE核心 (ZYWPECore)   竹影清风制作  FirTeam出品
 *
 *  完全自绘、完全独立，不依赖 explorer.exe（WinPE 中本就没有 explorer）：
 *    · 自定义窗体（可拖动 / 缩放 / 最大化，无系统标题栏）
 *    · 工具栏：后退 / 前进 / 上级 / 刷新 / 新建文件夹 + 可编辑地址栏
 *    · 左栏：此电脑（各盘符 + 卷标 + 容量）+ 常用位置
 *    · 右栏：文件列表（名称 / 大小 / 类型 / 修改日期），列头点击排序
 *    · 多选（Ctrl / Shift）、滚轮、自绘滚动条
 *    · 右键菜单：打开 / 用记事本打开 / 复制 / 剪切 / 粘贴 / 删除 / 重命名 / 新建文件夹 / 属性
 *    · 快捷键：Enter Backspace Delete F2 F5 Ctrl+A Ctrl+C Ctrl+X Ctrl+V
 *    · 文件图标**优先**取自随程序自带的 zywico.dll（竹影清风图标库，内容来自
 *      winico\shell32 的「二创太阳谷」图标，只保留 ≤48px 帧以压缩体积；
 *      资源 ID = shell32.dll 的图标索引，见 kKindIconId 注释）。查找顺序：
 *      程序同目录 → System32。自带库覆盖不到的类型（IK_FILE 通用文件、
 *      以及 exe/lnk/msi 这类「软件」）才去问系统的**默认应用关联**要图标，
 *      问不到就回退自带图标 —— PE 的系统 imageres.dll 被裁到只剩 12KB
 *      （空壳、没有任何图标资源），系统图像列表全是空图标，所以每次取到
 *      系统图标都会先做一次「是不是空图」探测，空就整条禁用，绝不会出现
 *      「有的有图标、有的没有」。见 AssocIcon() 的注释。
 *    · 首页（此电脑）为 Win10 资源管理器风格的「设备和驱动器」网格视图：
 *      大图标 + 卷标 + 蓝色容量条 + 「xx GB 可用，共 xx GB」
 ******************************************************************************/
#include "zywpe.h"
#include "theme.h"
#include <shellapi.h>
#include <algorithm>
#include <map>

/* ============================ 尺寸 ============================ */
/* 全部按 96 DPI 的「逻辑像素」书写，S() 换算成当前 DPI 的物理像素。
 * 标题栏高 / 标题栏按钮宽 / 侧栏宽 与 iced 版「竹影清风PE工具合集」同值，
 * 见 theme.h。其余尺寸是本程序自己的布局参数。 */
#define FM_TITLE_H    S(T_TITLE_H)   /* 38  标题栏            */
#define FM_TOOL_H     S(46)          /*     工具栏            */
#define FM_LEFTW      S(T_SIDE_W)    /* 196 侧栏（盘符）      */
#define FM_HEAD_H     S(28)          /*     列头              */
#define FM_ROW_H      S(26)          /*     文件行高          */
#define FM_STAT_H     S(26)          /*     状态栏            */
#define FM_SB_W       S(13)          /*     自绘滚动条宽      */
#define FM_BTN_W      S(30)          /*     工具栏按钮宽      */
#define FM_BTN_H      S(28)          /*     工具栏按钮高      */
#define FM_TBTN_W     S(T_WINBTN_W)  /* 46  标题栏按钮宽      */
#define FM_TBTN_N     3              /*     最小化 / 最大化 / 关闭 */
#define FM_RADIUS     S(6)           /*     圆角半径（同 iced 版） */

/* 标题栏按钮序号（FmTitleBtnRect / FmTitleBtnAt 的返回值） */
enum { TB_MIN = 0, TB_MAX, TB_CLOSE, TB_COUNT };

/* 工具栏按钮 */
enum { FB_BACK = 0, FB_FWD, FB_UP, FB_REFRESH, FB_NEWDIR, FB_COUNT };

/* 排序方式 */
enum { SORT_NAME = 0, SORT_SIZE, SORT_TYPE, SORT_DATE };

/* 前置声明：自绘输入框（定义见文件末尾） */
int ZywInputBox(HWND owner, const wchar_t* title, const wchar_t* prompt, std::wstring& value);

/* ============================ 数据结构 ============================ */
struct FmItem {
    std::wstring name;
    std::wstring full;
    std::wstring type;
    bool         isDir;
    bool         isDrive;
    ULONGLONG    size;
    ULONGLONG    freeB = 0;      /* 驱动器：可用空间（非驱动器恒为 0）      */
    UINT         driveType = 0;  /* 驱动器：GetDriveTypeW 结果（决定图标）   */
    FILETIME     mt;
    DWORD        attr;
};

struct FmWin {
    HWND         hwnd;
    HWND         hEdit;                 /* 地址栏编辑框 */
    std::wstring path;                  /* 空 = 此电脑 */
    std::vector<FmItem> items;
    std::vector<char>   sel;            /* 每项是否选中 */
    std::vector<std::wstring> hist;
    int          histPos;
    int          anchor;                /* 多选锚点 */
    int          hover;
    int          top;
    int          hovBtn;
    int          hovDrive;
    int          hovTitleBtn;           /* 标题栏按钮悬停：TB_MIN/TB_MAX/TB_CLOSE，-1 无 */
    bool         tracking;              /* 是否已向系统登记 WM_MOUSELEAVE */
    bool         dragSb;
    int          dragY0, dragTop0;
    int          sortBy;
    bool         sortAsc;
};

static std::vector<FmWin*> g_fms;
static bool   g_fmClassReg = false;

/* 内部剪贴板 */
static std::vector<std::wstring> g_clip;
static bool g_clipCut = false;

/* ============================ 小工具 ============================ */
static void ToLowerW(std::wstring& s)
{
    for (size_t i = 0; i < s.size(); i++)
        if (s[i] >= L'A' && s[i] <= L'Z') s[i] += 32;
}

static bool IsDirAttr(DWORD a) { return (a & FILE_ATTRIBUTE_DIRECTORY) != 0; }

/* ==================== 图标：随程序的 zywico.dll（winico 二创太阳谷） ==================== */
/*  ★ 一律用自带图标，**不用系统图标**：PE 的 imageres.dll 被裁到只剩 12KB
 *  （空壳、一个图标资源都没有），系统图像列表里全是空图标 —— SHGetFileInfoW
 *  照样「成功」返回索引，但 ImageList_Draw 画出来是全透明的，表现就是
 *  **所有文件都没有图标**；而「系统可用就用系统」的混用还会让同一屏里
 *  有的有图标、有的没有。
 *  ★ 也不自绘：自绘和真实文件类型对不上，观感也差。
 *  程序自带 zywico.dll（= 竹影清风图标库，由 winico\shell32 的二创太阳谷图标
 *  精简编译而成，只保留 ≤48px 帧），按资源 ID 取**真图标**。
 *  查找顺序：程序同目录 zywico.dll → %SystemRoot%\System32\zywico.dll
 *           → %SystemRoot%\System32\imageres.dll（兜底） */

enum FmIconKind {
    IK_PC, IK_FOLDER, IK_DRIVE, IK_CD, IK_USB, IK_EXE, IK_TEXT,
    IK_IMAGE, IK_ARCHIVE, IK_AUDIO, IK_VIDEO, IK_PS1, IK_BAT,
    IK_DRIVER, IK_FILE, IK_KIND_COUNT
};

/* 各「种类」对应的图标资源 ID。
 * ★ 现在用的是 winico\shell32 这套「二创太阳谷」图标（用户指定，从 shell32.dll
 *   提取，文件名 shell32.dll(N).ico 的 N 即资源 ID，1-based）。
 *   语义按 shell32.dll 长期稳定的索引约定：
 *       1 默认/无扩展名文件   3 默认应用(exe)   4 文件夹
 *       8 移动硬盘/U盘        9 本地硬盘         12 光驱        16 我的电脑
 *       152 txt 文本          153 批处理(bat)    154 dll/sys
 *       160 PowerShell       173 压缩包          224 视频
 *       41  音频              226 图片
 *   dll 由 winico\_trim\winico.rc 编译而成（只保留 ≤48px 帧、重复帧去重，压体积）。
 *   选号原则：同一语义有多个候选时，挑帧序列最全（含 16/32/48）的那个，
 *   这样列表、平铺、磁贴三种尺寸都不会被拉伸糊掉。
 *   想换别的图标：改这里的 ID（从 winico\shell32 里挑）→ 重新编译即可。 */
static const int kKindIconId[IK_KIND_COUNT] = {
    16,     /* IK_PC       此电脑（我的电脑）   */
    4,      /* IK_FOLDER   文件夹               */
    9,      /* IK_DRIVE    本地磁盘（硬盘）     */
    12,     /* IK_CD       光盘驱动器           */
    8,      /* IK_USB      可移动存储（U盘）    */
    3,      /* IK_EXE      应用程序             */
    152,    /* IK_TEXT     文本文档             */
    226,    /* IK_IMAGE    图片文件             */
    173,    /* IK_ARCHIVE  压缩包               */
    41,     /* IK_AUDIO    音频（音乐）         */
    224,    /* IK_VIDEO    视频                 */
    160,    /* IK_PS1      PowerShell 脚本      */
    153,    /* IK_BAT      批处理 bat / cmd     */
    154,    /* IK_DRIVER   dll / sys            */
    1,      /* IK_FILE     通用文件             */
};

static HINSTANCE  g_imgres          = NULL;
static bool       g_imgres_done     = false;
static std::wstring g_imgres_path;
static HICON      g_kindIcon[IK_KIND_COUNT]   = { 0 };
static int        g_kindIconSz[IK_KIND_COUNT] = { 0 };

/* 打开内置图标资源 DLL（只在第一次调用时查找） */
static HINSTANCE ImgResModule()
{
    if (g_imgres_done) return g_imgres;
    g_imgres_done = true;

    wchar_t buf[MAX_PATH] = { 0 };
    /* 1) 程序同目录（随程序自带的那一份 zywico.dll） */
    if (GetModuleFileNameW(NULL, buf, MAX_PATH)) {
        std::wstring p(buf);
        size_t k = p.find_last_of(L"\\/");
        if (k != std::wstring::npos) {
            p.resize(k + 1);
            p += L"zywico.dll";
            if (GetFileAttributesW(p.c_str()) != INVALID_FILE_ATTRIBUTES) {
                g_imgres_path = p;
                g_imgres = LoadLibraryExW(p.c_str(), NULL, LOAD_LIBRARY_AS_DATAFILE);
            }
        }
    }
    /* 2) 系统目录：zywico.dll → imageres.dll（兜底） */
    if (!g_imgres && GetSystemDirectoryW(buf, MAX_PATH)) {
        const wchar_t* names[] = { L"\\zywico.dll", L"\\imageres.dll" };
        for (int i = 0; i < 2 && !g_imgres; i++) {
            std::wstring p = std::wstring(buf) + names[i];
            if (GetFileAttributesW(p.c_str()) != INVALID_FILE_ATTRIBUTES) {
                g_imgres_path = p;
                g_imgres = LoadLibraryExW(p.c_str(), NULL, LOAD_LIBRARY_AS_DATAFILE);
            }
        }
    }
    return g_imgres;
}

/* 按「种类」取内置图标（按尺寸缓存） */
static HICON KindIcon(FmIconKind k, int sz)
{
    if (k < 0 || k >= IK_KIND_COUNT) return NULL;
    if (sz <= 0) sz = 16;
    if (g_kindIcon[k] && g_kindIconSz[k] == sz) return g_kindIcon[k];
    if (g_kindIcon[k]) { DestroyIcon(g_kindIcon[k]); g_kindIcon[k] = NULL; }

    HINSTANCE h = ImgResModule();
    if (h)
        g_kindIcon[k] = (HICON)LoadImageW(h, MAKEINTRESOURCEW(kKindIconId[k]),
                                          IMAGE_ICON, sz, sz, LR_DEFAULTCOLOR);
    /* LOAD_LIBRARY_AS_DATAFILE 的句柄个别情况下 LoadImage 不认 → 退回普通加载 */
    if (!g_kindIcon[k] && !g_imgres_path.empty()) {
        HINSTANCE h2 = LoadLibraryExW(g_imgres_path.c_str(), NULL, 0);
        if (h2)
            g_kindIcon[k] = (HICON)LoadImageW(h2, MAKEINTRESOURCEW(kKindIconId[k]),
                                              IMAGE_ICON, sz, sz, LR_DEFAULTCOLOR);
    }
    g_kindIconSz[k] = sz;
    return g_kindIcon[k];
}

static bool ExtIn(const std::wstring& e, const wchar_t* const* list, int n)
{
    for (int i = 0; i < n; i++) if (e == list[i]) return true;
    return false;
}

static FmIconKind IconKindOf(const std::wstring& name, bool isDir, bool isDrive)
{
    if (isDrive) return IK_DRIVE;
    if (isDir)   return IK_FOLDER;

    std::wstring ext;
    size_t d = name.rfind(L'.');
    if (d != std::wstring::npos && d + 1 < name.size()) ext = name.substr(d + 1);
    ToLowerW(ext);

    /* ★ bat/cmd、ps1 已从 kExe 里挪出来，各自走独立图标 */
    static const wchar_t* kExe[] = { L"exe", L"com", L"msi", L"dll", L"sys",
                                     L"scr", L"vbs", L"lnk", L"ocx", L"cpl", L"drv" };
    static const wchar_t* kTxt[] = { L"txt", L"ini", L"log", L"md", L"json", L"xml", L"cfg",
                                     L"conf", L"csv", L"htm", L"html", L"js", L"css", L"reg",
                                     L"inf", L"cat", L"wcs", L"e", L"ec" };
    static const wchar_t* kImg[] = { L"jpg", L"jpeg", L"png", L"bmp", L"gif", L"ico", L"webp",
                                     L"tif", L"tiff", L"svg", L"psd" };
    static const wchar_t* kArc[] = { L"zip", L"7z", L"rar", L"cab", L"wim", L"esd", L"gz", L"tar",
                                     L"iso", L"lzma", L"zst", L"xz", L"arj", L"ace" };
    /* 音频 / 视频拆成两类，各自一个图标 */
    static const wchar_t* kAud[] = { L"mp3", L"wav", L"flac", L"ape", L"m4a", L"ogg", L"wma",
                                     L"aac", L"opus", L"mid", L"midi", L"amr" };
    static const wchar_t* kVid[] = { L"mp4", L"avi", L"mkv", L"mov", L"wmv", L"flv", L"rmvb",
                                     L"mpg", L"mpeg", L"ts", L"webm", L"m4v", L"3gp" };
    static const wchar_t* kPs1[] = { L"ps1", L"psm1", L"psd1" };
    static const wchar_t* kBat[] = { L"bat", L"cmd" };
    static const wchar_t* kDrv[] = { L"inf", L"sys", L"drv" };

    if (ExtIn(ext, kPs1, _countof(kPs1))) return IK_PS1;
    if (ExtIn(ext, kBat, _countof(kBat))) return IK_BAT;
    if (ExtIn(ext, kExe, _countof(kExe))) return IK_EXE;
    if (ExtIn(ext, kImg, _countof(kImg))) return IK_IMAGE;
    if (ExtIn(ext, kArc, _countof(kArc))) return IK_ARCHIVE;
    if (ExtIn(ext, kAud, _countof(kAud))) return IK_AUDIO;
    if (ExtIn(ext, kVid, _countof(kVid))) return IK_VIDEO;
    if (ExtIn(ext, kTxt, _countof(kTxt))) return IK_TEXT;
    if (ExtIn(ext, kDrv, _countof(kDrv))) return IK_DRIVER;
    return IK_FILE;
}

/* 画一个「种类」图标（来自内置 zywico.dll，不再是自绘图形） */
static void DrawIconKind(HDC dc, int x, int y, int sz, FmIconKind k)
{
    HICON ic = KindIcon(k, sz);
    if (!ic) return;
    DrawIconEx(dc, x, y, ic, sz, sz, 0, NULL, DI_NORMAL);
}

/* ==================== 系统「默认应用关联」图标（补空缺用） ====================
 * 自带 zywico.dll 覆盖了 15 类常见文件，剩下的（.docx / .pdf / .xlsx / 无扩展名…）
 * 一律落到同一张「通用文件」图标上，看不出差别。这类文件本来就能问系统要：
 * SHGetFileInfoW 会顺着扩展名找到**默认关联程序**（Word、Edge、7-Zip…）并给出
 * 它的图标；exe / lnk / msi 甚至能取到文件自身内嵌的那张。
 *
 * ★ 老坑必须防：PE 里 imageres.dll 常被裁成 12KB 空壳，这时 SHGetFileInfoW
 *   照样「成功」返回句柄，但画出来**全透明**（就是当年所有文件都没图标的原因）。
 *   所以第一次取到系统图标后先做一次像素探测，确认不是空图才启用这条路径；
 *   一旦判定不可用就整条禁用，继续用自带图标 —— 绝不会出现
 *   「同一屏里有的有图标、有的没有」。自带库覆盖到的类型也不抢，风格保持一致。 */

static bool                        g_assocProbed = false;
static bool                        g_assocUsable = false;
static std::map<std::wstring, HICON> g_assocSmall;   /* 16~24px 那档 */
static std::map<std::wstring, HICON> g_assocLarge;   /* 32px 及以上   */

/* 图标是不是「空的」（全透明）—— PE 空壳 imageres.dll 的典型症状 */
static bool IconHasPixels(HICON ic, int sz)
{
    if (!ic) return false;
    HDC dc = CreateCompatibleDC(NULL);
    if (!dc) return false;
    BITMAPINFO bi = { 0 };
    bi.bmiHeader.biSize        = sizeof(BITMAPINFOHEADER);
    bi.bmiHeader.biWidth       = sz;
    bi.bmiHeader.biHeight      = -sz;               /* top-down，扫描顺序直观 */
    bi.bmiHeader.biPlanes      = 1;
    bi.bmiHeader.biBitCount    = 32;
    bi.bmiHeader.biCompression = BI_RGB;
    void* bits = NULL;
    HBITMAP dib = CreateDIBSection(dc, &bi, DIB_RGB_COLORS, &bits, NULL, 0);
    if (!dib || !bits) { DeleteDC(dc); return false; }
    HGDIOBJ old = SelectObject(dc, dib);
    memset(bits, 0, (size_t)sz * sz * 4);
    DrawIconEx(dc, 0, 0, ic, sz, sz, 0, NULL, DI_NORMAL);
    SelectObject(dc, old);
    const unsigned char* p = (const unsigned char*)bits;
    bool any = false;
    for (int i = 3; i < sz * sz * 4; i += 4) if (p[i] != 0) { any = true; break; }
    DeleteObject(dib);
    DeleteDC(dc);
    return any;
}

/* fromFile=true 时不加 SHGFI_USEFILEATTRIBUTES：会真的去读文件里的图标资源
 * （exe / lnk / msi 自己的那张），慢一点但准确；false 只看扩展名，很快。 */
static HICON ShellAssocIcon(const wchar_t* path, DWORD attr, int sz, bool fromFile)
{
    SHFILEINFOW sfi = { 0 };
    UINT fl = SHGFI_ICON | (sz <= 24 ? SHGFI_SMALLICON : SHGFI_LARGEICON);
    if (!fromFile) fl |= SHGFI_USEFILEATTRIBUTES;
    if (!SHGetFileInfoW(path, attr, &sfi, sizeof(sfi), fl)) return NULL;
    return sfi.hIcon;
}

static HICON AssocIcon(const FmItem& it, int sz)
{
    if (!g_assocProbed) {                 /* 只探测一次：系统图标到底能不能用 */
        g_assocProbed = true;
        HICON t = ShellAssocIcon(L"probe.txt", FILE_ATTRIBUTE_NORMAL, sz, false);
        g_assocUsable = IconHasPixels(t, sz);
        if (t) DestroyIcon(t);
    }
    if (!g_assocUsable) return NULL;

    /* 缓存键：可执行类按完整路径（每个文件的图标都不同），其余按扩展名 */
    std::wstring ext;
    size_t d = it.name.rfind(L'.');
    if (d != std::wstring::npos && d + 1 < it.name.size()) ext = it.name.substr(d + 1);
    ToLowerW(ext);
    bool exec = (ext == L"exe" || ext == L"lnk" || ext == L"scr" ||
                 ext == L"msi" || ext == L"com");
    std::wstring key = exec ? it.full : (L"*." + ext);

    std::map<std::wstring, HICON>& cache = (sz <= 24) ? g_assocSmall : g_assocLarge;
    std::map<std::wstring, HICON>::iterator f = cache.find(key);
    if (f != cache.end()) return f->second;        /* 拿不到也记着，避免每帧重试 */
    if (cache.size() > 256) {                      /* 简单上限：超了整表重建 */
        for (f = cache.begin(); f != cache.end(); ++f) if (f->second) DestroyIcon(f->second);
        cache.clear();
    }
    HICON ic = ShellAssocIcon(it.full.c_str(), it.attr, sz, exec);
    if (ic && !IconHasPixels(ic, sz)) { DestroyIcon(ic); ic = NULL; }
    cache[key] = ic;
    return ic;
}

/* 条目最终用的图标：自带库没覆盖到的类型（含 exe 自身图标）先问系统关联，
 * 问不到再回退自带 zywico 图标 */
static HICON ItemIcon(const FmItem& it, int sz)
{
    FmIconKind k = IconKindOf(it.name, it.isDir, it.isDrive);
    if (!it.isDir && !it.isDrive && (k == IK_FILE || k == IK_EXE)) {
        HICON a = AssocIcon(it, sz);
        if (a) return a;
    }
    return KindIcon(k, sz);
}

static void DrawItemIcon(HDC dc, int x, int y, int sz, const FmItem& it)
{
    HICON ic = ItemIcon(it, sz);
    if (!ic) return;
    DrawIconEx(dc, x, y, ic, sz, sz, 0, NULL, DI_NORMAL);
}

/* 驱动器按类型选图标（硬盘 / U盘 / 光盘），与系统资源管理器一致 */
static FmIconKind DriveIconKind(UINT driveType)
{
    if (driveType == DRIVE_CDROM)     return IK_CD;
    if (driveType == DRIVE_REMOVABLE) return IK_USB;
    return IK_DRIVE;
}

/* 3 位有效数字的字节数（对齐 Win10 资源管理器的显示习惯：
 * 「34.5 GB」「300 GB」「7.95 GB」「256 MB」「0 字节」） */
static std::wstring ByteSizeText(ULONGLONG v)
{
    wchar_t b[64];
    auto f3 = [&](const wchar_t* unit, double x) {
        if      (x >= 100.0) swprintf_s(b, L"%.0f %s", x, unit);
        else if (x >= 10.0)  swprintf_s(b, L"%.1f %s", x, unit);
        else                 swprintf_s(b, L"%.2f %s", x, unit);
    };
    if      (v < 1024ULL)                 swprintf_s(b, L"%llu 字节", v);
    else if (v < (1024ULL << 10))         f3(L"KB", v / 1024.0);
    else if (v < (1024ULL << 20))         f3(L"MB", v / 1048576.0);
    else if (v < (1024ULL << 30))         f3(L"GB", v / 1073741824.0);
    else                                  f3(L"TB", v / 1099511627776.0);
    return b;
}

/* 「此电脑」磁贴第三行文字：「34.5 GB 可用，共 300 GB」 */
static std::wstring DriveCapText(const FmItem& it)
{
    if (it.size == 0 && it.freeB == 0) return L"";
    if (it.size == 0) return ByteSizeText(it.freeB) + L" 可用";
    return ByteSizeText(it.freeB) + L" 可用，共 " + ByteSizeText(it.size);
}

static std::wstring SizeText(ULONGLONG n, bool isDir)
{
    if (isDir) return L"";
    wchar_t b[64];
    if (n < 1024)                swprintf_s(b, L"%llu B", n);
    else if (n < 1024ULL * 1024) swprintf_s(b, L"%.1f KB", n / 1024.0);
    else if (n < 1024ULL * 1024 * 1024) swprintf_s(b, L"%.1f MB", n / 1048576.0);
    else                         swprintf_s(b, L"%.2f GB", n / 1073741824.0);
    return b;
}

static std::wstring DateText(const FILETIME& ft)
{
    if (ft.dwLowDateTime == 0 && ft.dwHighDateTime == 0) return L"";   /* 驱动器无时间 */
    FILETIME lt;
    if (!FileTimeToLocalFileTime(&ft, &lt)) return L"";
    SYSTEMTIME st;
    if (!FileTimeToSystemTime(&lt, &st)) return L"";
    if (st.wYear < 1980) return L"";                                    /* 无效时间 */
    wchar_t b[64];
    swprintf_s(b, L"%04d-%02d-%02d %02d:%02d",
               st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute);
    return b;
}

static std::wstring JoinPath(const std::wstring& dir, const std::wstring& name)
{
    if (dir.empty()) return name;
    if (dir.back() == L'\\') return dir + name;
    return dir + L"\\" + name;
}

static std::wstring ParentOf(const std::wstring& p)
{
    if (p.size() <= 3) return L"";                    /* "C:\" → 此电脑 */
    std::wstring s = p;
    while (!s.empty() && s.back() == L'\\') s.pop_back();
    size_t k = s.rfind(L'\\');
    if (k == std::wstring::npos) return L"";
    std::wstring up = s.substr(0, k);
    if (up.size() == 2 && up[1] == L':') up += L"\\";
    return up;
}

/* （原来的 LeafOf —— 取路径末段名字 —— 只被标题栏用过。
 *  9.2 起标题栏改为固定显示程序名，当前路径交给地址栏，这个函数已无引用，删除。） */

/* ============================ 读取目录 ============================ */
static void AddDrive(std::vector<FmItem>& out, wchar_t letter)
{
    wchar_t root[4] = { letter, L':', L'\\', 0 };
    FmItem it = {};
    it.full   = root;
    it.isDir  = true;
    it.isDrive = true;
    it.size   = 0;
    it.attr   = FILE_ATTRIBUTE_DIRECTORY;

    wchar_t label[MAX_PATH] = { 0 };
    UINT dt = GetDriveTypeW(root);
    const wchar_t* kind = L"\u672C\u5730\u78C1\u76D8";
    if (dt == DRIVE_REMOVABLE)     kind = L"\u53EF\u79FB\u52A8\u5B58\u50A8";
    else if (dt == DRIVE_CDROM)    kind = L"\u5149\u76D8\u9A71\u52A8\u5668";
    else if (dt == DRIVE_REMOTE)   kind = L"\u7F51\u7EDC\u9A71\u52A8\u5668";
    else if (dt == DRIVE_RAMDISK)  kind = L"RAM \u78C1\u76D8";

    if (!GetVolumeInformationW(root, label, MAX_PATH, NULL, NULL, NULL, NULL, 0) || !label[0])
        wcscpy_s(label, kind);
    it.name = std::wstring(label) + L" (" + letter + L":)";
    it.type = kind;
    it.driveType = dt;

    ULARGE_INTEGER freeB = { 0 }, totalB = { 0 };
    GetDiskFreeSpaceExW(root, &freeB, &totalB, NULL);
    if (totalB.QuadPart > 0) it.size = totalB.QuadPart;   /* 容量列显示总容量 */
    it.freeB = freeB.QuadPart;

    out.push_back(it);
}

static void LoadDir(FmWin* w)
{
    w->items.clear();
    w->sel.clear();
    w->hover = -1;
    w->anchor = -1;

    if (w->path.empty()) {                            /* 此电脑 */
        DWORD drives = GetLogicalDrives();
        for (int i = 0; i < 26; i++) {
            if (!(drives & (1u << i))) continue;
            wchar_t root[4] = { (wchar_t)(L'A' + i), L':', L'\\', 0 };
            UINT t = GetDriveTypeW(root);
            if (t == DRIVE_NO_ROOT_DIR || t == DRIVE_UNKNOWN) continue;
            AddDrive(w->items, (wchar_t)(L'A' + i));
        }
    } else {
        WIN32_FIND_DATAW fd;
        HANDLE h = FindFirstFileW(JoinPath(w->path, L"*").c_str(), &fd);
        if (h != INVALID_HANDLE_VALUE) {
            bool first = true;
            while (first || FindNextFileW(h, &fd)) {
                first = false;
                std::wstring nm = fd.cFileName;
                if (nm == L"." || nm == L"..") continue;
                FmItem it;
                it.name  = nm;
                it.full  = JoinPath(w->path, nm);
                it.attr  = fd.dwFileAttributes;
                it.isDir = IsDirAttr(fd.dwFileAttributes);
                it.isDrive = false;
                it.size  = ((ULONGLONG)fd.nFileSizeHigh << 32) | fd.nFileSizeLow;
                it.mt    = fd.ftLastWriteTime;

                /* 类型列的文字仍取 Shell 的描述（「应用程序」「文本文档」…），
                 * 图标不再取系统图像列表，一律走自带的 imageres.dll。 */
                SHFILEINFOW sfi = { 0 };
                if (SHGetFileInfoW(it.full.c_str(), fd.dwFileAttributes, &sfi, sizeof(sfi),
                                   SHGFI_TYPENAME))
                    it.type = sfi.szTypeName;
                else
                    it.type = it.isDir ? L"\u6587\u4EF6\u5939" : L"\u6587\u4EF6";
                w->items.push_back(it);
            }
            FindClose(h);
        }
    }

    w->sel.assign(w->items.size(), 0);
    w->top = 0;
    w->sortBy = SORT_NAME;
    w->sortAsc = true;

    /* 默认排序：文件夹优先 + 名称。
     * 「此电脑」视图保持盘符字母顺序（GetLogicalDrives 的枚举顺序），不重排。 */
    if (!w->path.empty())
        std::stable_sort(w->items.begin(), w->items.end(), [](const FmItem& a, const FmItem& b) {
            if (a.isDir != b.isDir) return a.isDir > b.isDir;
            std::wstring x = a.name, y = b.name;
            ToLowerW(x); ToLowerW(y);
            return x < y;
        });
}

static void SortItems(FmWin* w)
{
    int by = w->sortBy; bool asc = w->sortAsc;
    std::stable_sort(w->items.begin(), w->items.end(),
                     [by, asc](const FmItem& a, const FmItem& b) {
        if (a.isDir != b.isDir) return a.isDir > b.isDir;      /* 目录恒在前 */
        int r = 0;
        switch (by) {
        case SORT_SIZE: r = (a.size < b.size) ? -1 : (a.size > b.size ? 1 : 0); break;
        case SORT_TYPE: r = _wcsicmp(a.type.c_str(), b.type.c_str()); break;
        case SORT_DATE: r = CompareFileTime(&a.mt, &b.mt); break;
        default: {
            std::wstring x = a.name, y = b.name;
            ToLowerW(x); ToLowerW(y);
            r = x.compare(y);
            break;
        }
        }
        return asc ? (r < 0) : (r > 0);
    });
}

/* ============================ 布局 ============================ */
static int  FmListTop()   { return FM_TITLE_H + FM_TOOL_H + FM_HEAD_H; }
static int  FmToolY()     { return FM_TITLE_H; }
static RECT FmBtnRect(int i)
{
    int y = FM_TITLE_H + (FM_TOOL_H - FM_BTN_H) / 2;
    int x = S(8) + i * (FM_BTN_W + S(2));
    return { x, y, x + FM_BTN_W, y + FM_BTN_H };
}
static RECT FmAddrRect(int W)
{
    int y = FM_TITLE_H + (FM_TOOL_H - S(26)) / 2;
    int x = S(8) + FB_COUNT * (FM_BTN_W + S(2)) + S(6);
    return { x, y, W - S(10), y + S(26) };
}
static int  FmStatusTop(HWND h)
{
    RECT cr; GetClientRect(h, &cr);
    return cr.bottom - FM_STAT_H;
}
/* 工具栏按钮命中：返回按钮序号，-1 = 没命中。
 * ★ 画（FmPaint）和点（WM_LBUTTONDOWN）必须用同一个 FmBtnRect，
 *   否则改一个尺寸就会「看着在这、点着在那」。 */
static int  FmToolBtnAt(int x, int y)
{
    for (int i = 0; i < FB_COUNT; i++) {
        RECT b = FmBtnRect(i);
        if (x >= b.left && x < b.right && y >= b.top && y < b.bottom) return i;
    }
    return -1;
}
static int FmRowsVisible(HWND h)
{
    int n = (FmStatusTop(h) - FmListTop()) / FM_ROW_H;
    return n < 1 ? 1 : n;
}
/* 前置声明：「此电脑」网格视图的度量函数（定义在下方） */
static bool FmPcView(const FmWin* w);
struct FmGridMetrics {
    int x0, y0;        /* 磁贴区左上角            */
    int cols;          /* 列数                    */
    int tileW, tileH;  /* 磁贴尺寸                */
    int colSpan, rowSpan; /* 含间距的步长          */
    int rowsVis;       /* 可见行数                */
};
static FmGridMetrics FmGridMetricsOf(HWND h, const FmWin* w, bool withSb);
static int FmGridRows(const FmWin* w, int cols);

static bool FmNeedScroll(FmWin* w)
{
    if (FmPcView(w)) {
        FmGridMetrics g = FmGridMetricsOf(w->hwnd, w, false);
        return FmGridRows(w, g.cols) > g.rowsVis;
    }
    return (int)w->items.size() > FmRowsVisible(w->hwnd);
}

/* ==================== 「此电脑」网格视图（设备和驱动器） ==================== */
/*  仿 Win10 资源管理器首页：组标题「设备和驱动器」+ 磁贴网格。
 *  每个磁贴 = 大图标(48) + 卷标(X:) + 蓝色容量条 + 「xx GB 可用，共 xx GB」。
 *  ★ w->top 在此视图下的语义变为「首个可见行」（一行的磁贴数 = cols）。 */

/* 是否处于「此电脑」首页（网格视图） */
static bool FmPcView(const FmWin* w) { return w->path.empty(); }

#define FM_TILE_W     S(310)     /* 磁贴宽   */
#define FM_TILE_H     S(78)      /* 磁贴高   */
#define FM_TILE_GX    S(10)      /* 列间距   */
#define FM_TILE_GY    S(8)       /* 行间距   */
#define FM_GRID_HDR   S(32)      /* 组标题高 */

static FmGridMetrics FmGridMetricsOf(HWND h, const FmWin* w, bool withSb)
{
    RECT cr; GetClientRect(h, &cr);
    int statTop = FmStatusTop(h);
    FmGridMetrics g;
    g.tileW = FM_TILE_W;
    g.tileH = FM_TILE_H;
    g.colSpan = g.tileW + FM_TILE_GX;
    g.rowSpan = g.tileH + FM_TILE_GY;
    g.x0 = FM_LEFTW + S(14);
    int right = cr.right - (withSb ? FM_SB_W : 0) - S(10);
    int availW = right - g.x0;
    g.cols = availW / g.colSpan;
    if (g.cols < 1) g.cols = 1;
    g.y0 = FM_TITLE_H + FM_TOOL_H + FM_GRID_HDR;
    g.rowsVis = (statTop - g.y0) / g.rowSpan;
    if (g.rowsVis < 1) g.rowsVis = 1;
    return g;
}

static int FmGridRows(const FmWin* w, int cols)
{
    if (cols < 1) cols = 1;
    return ((int)w->items.size() + cols - 1) / cols;
}

/* 滚动参数统一出口：n / vis 在「此电脑」下按行计，详情视图下按条目计 */
static void FmScrollDims(FmWin* w, int& n, int& vis)
{
    if (FmPcView(w)) {
        FmGridMetrics g = FmGridMetricsOf(w->hwnd, w, true);
        n = FmGridRows(w, g.cols);
        vis = g.rowsVis;
    } else {
        n = (int)w->items.size();
        vis = FmRowsVisible(w->hwnd);
    }
}

/* 网格坐标 → 磁贴序号（线性下标，与 items 对应）；没命中返回 -1 */
static int FmTileAt(HWND h, FmWin* w, int x, int y)
{
    if (!FmPcView(w)) return -1;
    FmGridMetrics g = FmGridMetricsOf(h, w, FmNeedScroll(w));
    if (x < g.x0 || y < g.y0) return -1;
    int col = (x - g.x0) / g.colSpan;
    if (col >= g.cols) return -1;
    if (x - g.x0 - col * g.colSpan >= g.tileW) return -1;      /* 列间隙 */
    int row = (y - g.y0) / g.rowSpan;
    if (row >= FmGridRows(w, g.cols)) return -1;
    if (y - g.y0 - row * g.rowSpan >= g.tileH) return -1;      /* 行间隙 */
    int idx = row * g.cols + col;
    if (idx >= (int)w->items.size()) return -1;
    return idx;
}

/* 选中项 idx 滚动到可见范围（两种视图各自处理 top 语义） */
static void FmEnsureVisible(FmWin* w, int idx)
{
    if (FmPcView(w)) {
        FmGridMetrics g = FmGridMetricsOf(w->hwnd, w, true);
        int row = idx / g.cols;
        if (row < w->top) w->top = row;
        if (row >= w->top + g.rowsVis) w->top = row - g.rowsVis + 1;
    } else {
        int vis = FmRowsVisible(w->hwnd);
        if (idx < w->top) w->top = idx;
        if (idx >= w->top + vis) w->top = idx - vis + 1;
    }
}

/* 侧栏行：标题「位置」28px，接着「此电脑」34px，再往下每行 34px（行高 32 + 间距 2） */
#define FM_SIDE_ROW_H S(34)
#define FM_SIDE_ROW_V S(32)          /* 行本身高（比行距小 2，留出间隙） */
static int FmSideItemTop()  { return FM_TITLE_H + FM_TOOL_H + S(32); }   /* 「此电脑」 */
static int FmSideDriveTop() { return FM_TITLE_H + FM_TOOL_H + S(66); }   /* 第一个盘符 */

/* 侧栏行内的图标/文字位置。
 * ★ 图标边长必须取系统实际的小图标尺寸（100% 时 16、150% 时 24），
 *   写死 16 会让图标在行里偏下并且溢出，看起来就是"图标和文字上下错位"。 */
static int SideIconX(const RECT& r)          { return r.left + S(9); }
static int SideIconY(const RECT& r, int ic)  { return r.top + ((r.bottom - r.top) - ic) / 2; }
static int SideTextX(const RECT& r, int ic)  { return r.left + S(9) + ic + S(8); }

/* 文件列表行内的图标/文字位置（同样按系统实际图标边长算） */
static int RowIconX()                        { return FM_LEFTW + S(6); }
static int RowIconY(int y)                   { return y + (FM_ROW_H - SysSmallIconSize()) / 2; }
static int RowTextX(int ic)                  { return FM_LEFTW + S(6) + ic + S(6); }

/* 标题栏按钮：0 = 最小化（—）  1 = 最大化/还原（□ / 双框）  2 = 关闭（×）
 * 从右往左排：最后一个恒在最右，所以按钮区随 FM_TBTN_N 自动右对齐。 */
static RECT FmTitleBtnRect(int W, int i)
{
    return { W - (FM_TBTN_N - i) * FM_TBTN_W, 0,
             W - (FM_TBTN_N - 1 - i) * FM_TBTN_W, FM_TITLE_H };
}
static int FmTitleBtnAt(int W, int x, int y)
{
    if (y < 0 || y >= FM_TITLE_H) return -1;
    for (int i = 0; i < FM_TBTN_N; i++) {
        RECT b = FmTitleBtnRect(W, i);
        if (x >= b.left && x < b.right) return i;
    }
    return -1;
}

/* ============================ 绘制 ============================ */
/* 圆角矩形填充：border 传 CLR_INVALID 表示无边框（描边同填充色） */
static void FillRound(HDC dc, const RECT& r, int rad, COLORREF fill, COLORREF border)
{
    if (r.right <= r.left || r.bottom <= r.top) return;
    HBRUSH b = CreateSolidBrush(fill);
    HBRUSH ob = (HBRUSH)SelectObject(dc, b);
    HPEN p = CreatePen(PS_SOLID, 1, border == CLR_INVALID ? fill : border);
    HPEN op = (HPEN)SelectObject(dc, p);
    RoundRect(dc, r.left, r.top, r.right, r.bottom, rad, rad);
    SelectObject(dc, op); DeleteObject(p);
    SelectObject(dc, ob); DeleteObject(b);
}

/* 1px 水平线 */
static void HLine(HDC dc, int x1, int x2, int y, COLORREF c)
{
    HPEN p = CreatePen(PS_SOLID, 1, c);
    HPEN op = (HPEN)SelectObject(dc, p);
    MoveToEx(dc, x1, y, NULL); LineTo(dc, x2, y);
    SelectObject(dc, op); DeleteObject(p);
}

/* 1px 垂直线 */
static void VLine(HDC dc, int x, int y1, int y2, COLORREF c)
{
    HPEN p = CreatePen(PS_SOLID, 1, c);
    HPEN op = (HPEN)SelectObject(dc, p);
    MoveToEx(dc, x, y1, NULL); LineTo(dc, x, y2);
    SelectObject(dc, op); DeleteObject(p);
}
/* 工具栏图标：全部按 96 DPI 的逻辑尺寸绘制，S() 换算成物理像素。
 * 线宽也一起缩放，否则 150% 下图标会显得又细又小。 */
static void DrawToolBtn(HDC dc, RECT r, int id, bool hot, COLORREF c)
{
    if (hot) FillRound(dc, r, FM_RADIUS, T_HOVER, CLR_INVALID);
    int cx = (r.left + r.right) / 2, cy = (r.top + r.bottom) / 2;
    int pw = S(2) < 1 ? 1 : S(2);
    HPEN p = CreatePen(PS_SOLID, pw, c);
    HPEN op = (HPEN)SelectObject(dc, p);
    switch (id) {
    case FB_BACK:
        MoveToEx(dc, cx + S(5), cy - S(6), NULL);
        LineTo(dc, cx - S(3), cy);
        LineTo(dc, cx + S(5), cy + S(6));
        break;
    case FB_FWD:
        MoveToEx(dc, cx - S(5), cy - S(6), NULL);
        LineTo(dc, cx + S(3), cy);
        LineTo(dc, cx - S(5), cy + S(6));
        break;
    case FB_UP:
        MoveToEx(dc, cx - S(6), cy + S(1), NULL);
        LineTo(dc, cx, cy - S(6));
        LineTo(dc, cx + S(6), cy + S(1));
        MoveToEx(dc, cx, cy - S(6), NULL);
        LineTo(dc, cx, cy + S(6));
        break;
    case FB_REFRESH: {
        /* 圆环：缺口开在右侧；箭头压在缺口上端，指向左上 */
        int r2 = S(6);
        Arc(dc, cx - r2, cy - r2, cx + r2, cy + r2,
            cx + r2, cy - r2,          /* 起点：右上 45° */
            cx + r2, cy + r2);         /* 终点：右下 45°（逆时针走 270°） */
        MoveToEx(dc, cx + S(1), cy - r2 - S(3), NULL);
        LineTo(dc, cx + r2 + S(1), cy - r2 + S(1));
        LineTo(dc, cx - S(2), cy - r2 + S(1));
        break;
    }
    case FB_NEWDIR:
        /* 文件夹 + 加号 */
        MoveToEx(dc, cx - S(8), cy + S(6), NULL);
        LineTo(dc, cx - S(8), cy - S(5));
        LineTo(dc, cx - S(3), cy - S(5));
        LineTo(dc, cx - S(1), cy - S(8));
        LineTo(dc, cx + S(3), cy - S(8));
        LineTo(dc, cx + S(5), cy - S(5));
        LineTo(dc, cx + S(8), cy - S(5));
        LineTo(dc, cx + S(8), cy + S(6));
        LineTo(dc, cx - S(8), cy + S(6));
        MoveToEx(dc, cx, cy - S(2), NULL);
        LineTo(dc, cx, cy + S(3));
        MoveToEx(dc, cx - S(3), cy + S(1), NULL);
        LineTo(dc, cx + S(3), cy + S(1));
        break;
    }
    SelectObject(dc, op);
    DeleteObject(p);
}

static void DrawChevronSort(HDC dc, int x, int y, bool asc, COLORREF c)
{
    int pw = S(2) < 1 ? 1 : S(2);
    HPEN p = CreatePen(PS_SOLID, pw, c);
    HPEN op = (HPEN)SelectObject(dc, p);
    if (asc) {
        MoveToEx(dc, x - S(4), y + S(2), NULL);
        LineTo(dc, x, y - S(2));
        LineTo(dc, x + S(4), y + S(2));
    } else {
        MoveToEx(dc, x - S(4), y - S(2), NULL);
        LineTo(dc, x, y + S(2));
        LineTo(dc, x + S(4), y - S(2));
    }
    SelectObject(dc, op);
    DeleteObject(p);
}

/* ==================== 「此电脑」网格视图绘制 ==================== */
static void DrawPcGrid(FmWin* w, HDC mem, int statTop)
{
    RECT cr; GetClientRect(w->hwnd, &cr);
    int W = cr.right;
    bool withSb = FmNeedScroll(w);
    FmGridMetrics g = FmGridMetricsOf(w->hwnd, w, withSb);

    /* ---- 组标题：˅ 设备和驱动器 ---- */
    {
        int cy = FM_TITLE_H + FM_TOOL_H + FM_GRID_HDR / 2;
        HPEN p = CreatePen(PS_SOLID, S(2) < 1 ? 1 : S(2), T_SUB);
        HPEN op = (HPEN)SelectObject(mem, p);
        MoveToEx(mem, g.x0 + S(4), cy - S(3), NULL);
        LineTo(mem, g.x0 + S(8), cy + S(3));
        LineTo(mem, g.x0 + S(12), cy - S(3));
        SelectObject(mem, op); DeleteObject(p);
        RECT hr = { g.x0 + S(20), FM_TITLE_H + FM_TOOL_H,
                    g.x0 + S(220), FM_TITLE_H + FM_TOOL_H + FM_GRID_HDR };
        DrawTextR(mem, hr, L"\u8BBE\u5907\u548C\u9A71\u52A8\u5668", g_fontUI, T_TEXT);
    }

    if (w->items.empty()) {
        RECT r = { g.x0, g.y0 + S(8), W - S(20), g.y0 + S(32) };
        DrawTextR(mem, r, L"\uFF08\u6CA1\u6709\u53EF\u7528\u7684\u9A71\u52A8\u5668\uFF09",
                  g_fontSmall, T_FAINT);
        return;
    }

    /* ---- 磁贴 ---- */
    const int icSz = S(48);
    int n = (int)w->items.size();
    for (int i = 0; i < n; i++) {
        int row = i / g.cols, col = i % g.cols;
        int vr = row - w->top;
        if (vr < 0) continue;
        if (vr >= g.rowsVis) break;
        int x = g.x0 + col * g.colSpan;
        int y = g.y0 + vr * g.rowSpan;
        RECT tr = { x, y, x + g.tileW, y + g.tileH };

        bool s = w->sel[i] != 0;
        bool hov = (i == w->hover);
        if (s)      FillRound(mem, tr, FM_RADIUS, T_SEL, CLR_INVALID);
        else if (hov) FillRound(mem, tr, FM_RADIUS, T_HOVER, CLR_INVALID);

        const FmItem& it = w->items[i];
        DrawIconKind(mem, x + S(14), y + (g.tileH - icSz) / 2, icSz,
                     DriveIconKind(it.driveType));

        int tx = x + S(14) + icSz + S(12);
        int tw = tr.right - S(12) - tx;
        if (tw < S(60)) tw = S(60);

        /* 第一行：卷标 (X:) */
        RECT rn = { tx, y + S(8), tx + tw, y + S(28) };
        COLORREF tc = (it.attr & FILE_ATTRIBUTE_HIDDEN) ? T_FAINT : T_TEXT;
        DrawTextR(mem, rn, it.name.c_str(), g_fontUI, tc);

        /* 第二行：容量条（有容量且非光驱才画；用量 > 90% 转红） */
        bool hasBar = (it.driveType != DRIVE_CDROM) && it.size > 0;
        int barH = S(8);
        int barY = y + S(32);
        if (hasBar) {
            RECT bar = { tx, barY, tx + tw, barY + barH };
            FillRound(mem, bar, barH / 2, T_LINE, CLR_INVALID);
            double used = (it.freeB < it.size)
                          ? 1.0 - (double)it.freeB / (double)it.size : 0.0;
            int fw = (int)(tw * used + 0.5);
            if (used > 0.002) {
                if (fw < barH) fw = barH;
                if (fw > tw) fw = tw;
                RECT fr = { tx, barY, tx + fw, barY + barH };
                FillRound(mem, fr, barH / 2,
                          used > 0.9 ? T_DANGER : T_ACCENT, CLR_INVALID);
            }
        }

        /* 第三行：「xx GB 可用，共 xx GB」 */
        std::wstring cap = DriveCapText(it);
        if (!cap.empty()) {
            RECT rc2 = { tx, (hasBar ? barY + barH + S(4) : barY),
                         tx + tw, y + g.tileH - S(6) };
            DrawTextR(mem, rc2, cap.c_str(), g_fontSmall, T_SUB);
        }
    }
}
static void FmPaint(FmWin* w, HDC hdc)
{
    RECT cr; GetClientRect(w->hwnd, &cr);
    int W = cr.right, H = cr.bottom;

    HDC mem = CreateCompatibleDC(hdc);
    HBITMAP dib = CreateCompatibleBitmap(hdc, W, H);
    HGDIOBJ old = SelectObject(mem, dib);
    SetBkMode(mem, TRANSPARENT);

    /* ---- 标题栏（白底 · 同 iced 版工具合集的沉浸式标题栏） ---- */
    RECT tr = { 0, 0, W, FM_TITLE_H };
    {
        HBRUSH b = CreateSolidBrush(T_CARD);
        FillRect(mem, &tr, b); DeleteObject(b);
        HLine(mem, 0, W, FM_TITLE_H - 1, T_LINE);

        /* 标题栏文字固定为程序名（与 iced 版工具合集一致：标题栏只放名字，
         * 当前路径交给下面的地址栏，标题就不会被长路径挤爆）。 */
        std::wstring title = L"\u7AF9\u5F71\u6E05\u98CE\u6587\u4EF6\u8D44\u6E90\u7BA1\u7406\u5668";
        RECT r = { S(14), 0, W - FM_TBTN_N * FM_TBTN_W - S(8), FM_TITLE_H };
        DrawTextR(mem, r, title.c_str(), g_fontSmall, T_TEXT);
    }
    /* 窗口按钮：—  □  ×   （悬停变色，关闭键悬停转红底白字） */
    {
        bool zoomed = IsZoomed(w->hwnd) != FALSE;
        for (int i = 0; i < FM_TBTN_N; i++) {
            RECT b = FmTitleBtnRect(W, i);
            bool close = (i == TB_CLOSE);
            bool hot = (w->hovTitleBtn == i);
            if (hot) {
                HBRUSH hb = CreateSolidBrush(close ? T_CLOSE_HOVER : T_WIN_HOVER);
                FillRect(mem, &b, hb); DeleteObject(hb);
            }
            int cx = (b.left + b.right) / 2, cy = (b.top + b.bottom) / 2;
            COLORREF ink = (hot && close) ? T_CARD : T_TEXT;
            HPEN p = CreatePen(PS_SOLID, 1, ink);
            HPEN op = (HPEN)SelectObject(mem, p);
            HBRUSH ob = (HBRUSH)SelectObject(mem, GetStockObject(NULL_BRUSH));

            if (i == TB_MIN) {                                   /* — */
                MoveToEx(mem, cx - S(6), cy, NULL);
                LineTo(mem, cx + S(6), cy);
            } else if (i == TB_MAX) {                            /* □ / 还原 */
                if (!zoomed) {
                    Rectangle(mem, cx - S(5), cy - S(5), cx + S(5), cy + S(5));
                } else {
                    /* 还原：后面一个方框 + 前面一个错开的小方框 */
                    Rectangle(mem, cx - S(5), cy - S(3), cx + S(4), cy + S(6));
                    MoveToEx(mem, cx - S(3), cy - S(5), NULL);
                    LineTo(mem, cx + S(5), cy - S(5));
                    LineTo(mem, cx + S(5), cy + S(3));
                }
            } else {                                             /* × */
                MoveToEx(mem, cx - S(5), cy - S(5), NULL);
                LineTo(mem, cx + S(5), cy + S(5));
                MoveToEx(mem, cx + S(5), cy - S(5), NULL);
                LineTo(mem, cx - S(5), cy + S(5));
            }
            SelectObject(mem, ob);
            SelectObject(mem, op); DeleteObject(p);
        }
    }

    /* ---- 工具栏（白底，与标题栏连成一片） ---- */
    RECT br = { 0, FM_TITLE_H, W, FM_TITLE_H + FM_TOOL_H };
    {
        HBRUSH b = CreateSolidBrush(T_CARD);
        FillRect(mem, &br, b); DeleteObject(b);
        HLine(mem, 0, W, FM_TITLE_H + FM_TOOL_H - 1, T_LINE);
    }
    for (int i = 0; i < FB_COUNT; i++) {
        RECT b = FmBtnRect(i);
        bool enabled = true;
        if (i == FB_BACK)   enabled = (w->histPos > 0);
        if (i == FB_FWD)    enabled = (w->histPos + 1 < (int)w->hist.size());
        if (i == FB_UP)     enabled = !w->path.empty();
        if (i == FB_NEWDIR) enabled = !w->path.empty();   /* 「此电脑」里不能新建文件夹 */
        DrawToolBtn(mem, b, i, (i == w->hovBtn) && enabled,
                    enabled ? T_SUB : T_FAINT);
    }
    /* 地址栏外框（EDIT 覆盖在内部，白底由 WM_CTLCOLOREDIT 提供） */
    {
        RECT a = FmAddrRect(W);
        a.left -= 1; a.top -= 1; a.right += 1; a.bottom += 1;
        FillRound(mem, a, FM_RADIUS, T_CARD, T_LINE_STRONG);
    }

    /* ---- 左栏（浅灰侧栏 · 同 iced 版工具分类栏） ---- */
    int listTop = FmListTop();
    int statTop = FmStatusTop(w->hwnd);
    int sideTop = FM_TITLE_H + FM_TOOL_H;
    RECT lr = { 0, sideTop, FM_LEFTW, statTop };
    {
        HBRUSH b = CreateSolidBrush(T_SIDE);
        FillRect(mem, &lr, b); DeleteObject(b);
        VLine(mem, FM_LEFTW, sideTop, statTop, T_LINE);
    }

    /* 侧栏小标题「位置」（对应 iced 版的「工具分类」） */
    {
        RECT r = { S(12), sideTop, FM_LEFTW - S(12), sideTop + S(28) };
        DrawTextR(mem, r, L"\u4F4D\u7F6E", g_fontSmall, T_FAINT);
    }

    /* 左栏内容：此电脑 + 盘符（图标全部来自自带的 imageres.dll） */
    const int icSz = SysSmallIconSize();
    int ly = FmSideItemTop();
    {
        RECT r = { S(8), ly, FM_LEFTW - S(8), ly + FM_SIDE_ROW_V };
        bool sel = w->path.empty();
        bool hot = (w->hovDrive == -2);
        if (sel)        FillRound(mem, r, FM_RADIUS, T_SEL, CLR_INVALID);
        else if (hot)   FillRound(mem, r, FM_RADIUS, T_HOVER, CLR_INVALID);
        DrawIconKind(mem, SideIconX(r), SideIconY(r, icSz), icSz, IK_PC);
        RECT t = { SideTextX(r, icSz), r.top, r.right - S(8), r.bottom };
        DrawTextR(mem, t, L"\u6B64\u7535\u8111", g_fontUI, sel ? T_ACCENT : T_SUB);
        ly += FM_SIDE_ROW_H;
    }
    /* 盘符 */
    {
        DWORD drives = GetLogicalDrives();
        int di = 0;
        for (int i = 0; i < 26 && ly < statTop - S(8); i++) {
            if (!(drives & (1u << i))) continue;
            wchar_t root[4] = { (wchar_t)(L'A' + i), L':', L'\\', 0 };
            UINT t = GetDriveTypeW(root);
            if (t == DRIVE_NO_ROOT_DIR || t == DRIVE_UNKNOWN) continue;

            RECT r = { S(8), ly, FM_LEFTW - S(8), ly + FM_SIDE_ROW_V };
            bool cur = (w->path.size() >= 2 && w->path[0] == root[0] && w->path[1] == L':');
            bool hot = (w->hovDrive == di);
            if (cur)      FillRound(mem, r, FM_RADIUS, T_SEL, CLR_INVALID);
            else if (hot) FillRound(mem, r, FM_RADIUS, T_HOVER, CLR_INVALID);
            DrawIconKind(mem, SideIconX(r), SideIconY(r, icSz), icSz, DriveIconKind(t));
            RECT tr = { SideTextX(r, icSz), r.top, r.right - S(8), r.bottom };
            DrawTextR(mem, tr, root, g_fontUI, cur ? T_ACCENT : T_SUB);
            ly += FM_SIDE_ROW_H;
            di++;
        }
    }

    /* ---- 右栏内容：此电脑 = 网格视图；其余 = 详情列表 ---- */
    RECT rr = { FM_LEFTW + 1, sideTop, W, statTop };
    {
        HBRUSH b = CreateSolidBrush(T_CARD);
        FillRect(mem, &rr, b); DeleteObject(b);
    }

    if (FmPcView(w)) {
        DrawPcGrid(w, mem, statTop);
    } else {
    /* 列位置 */
    int right = W - (FmNeedScroll(w) ? FM_SB_W : 0) - S(6);
    int cDate = right - S(132);
    int cType = cDate - S(152);
    int cSize = cType - S(92);
    if (cSize < FM_LEFTW + S(200)) { cSize = FM_LEFTW + S(200); }

    /* 列头（白底 + 一条极细下边线，同 iced 版的 hline 分隔） */
    {
        RECT hr = { FM_LEFTW + 1, sideTop, W, listTop };
        HBRUSH b = CreateSolidBrush(T_CARD);
        FillRect(mem, &hr, b); DeleteObject(b);
        HLine(mem, FM_LEFTW + 1, W, listTop - 1, T_LINE);

        struct { const wchar_t* t; int x; int w; int id; UINT fmt; } cols[] = {
            { L"\u540D\u79F0",     RowTextX(icSz), cSize - RowTextX(icSz) - S(6), SORT_NAME, DT_LEFT },
            { L"\u5927\u5C0F",     cSize,         cType - cSize - S(8),     SORT_SIZE, DT_RIGHT },
            { L"\u7C7B\u578B",     cType,         cDate - cType - S(8),     SORT_TYPE, DT_LEFT },
            { L"\u4FEE\u6539\u65E5\u671F", cDate, right - cDate - S(4),     SORT_DATE, DT_LEFT },
        };
        for (int i = 0; i < 4; i++) {
            RECT r = { cols[i].x, sideTop, cols[i].x + cols[i].w, listTop };
            DrawTextR(mem, r, cols[i].t, g_fontSmall,
                      (w->sortBy == cols[i].id) ? T_ACCENT : T_SUB,
                      cols[i].fmt | DT_SINGLELINE | DT_VCENTER);
            if (w->sortBy == cols[i].id)
                DrawChevronSort(mem, cols[i].x + cols[i].w - S(8), (r.top + r.bottom) / 2,
                                w->sortAsc, T_ACCENT_SOFT);
        }
    }

    /* 行 */
    int vis = FmRowsVisible(w->hwnd);
    int n = (int)w->items.size();
    for (int i = w->top; i < n && i < w->top + vis; i++) {
        int y = listTop + (i - w->top) * FM_ROW_H;
        RECT r = { FM_LEFTW + 1, y, W - (FmNeedScroll(w) ? FM_SB_W : 0), y + FM_ROW_H };
        bool s = w->sel[i] != 0;
        bool h2 = (i == w->hover);
        if (s) {
            HBRUSH b = CreateSolidBrush(T_SEL);
            FillRect(mem, &r, b); DeleteObject(b);
        } else if (h2) {
            HBRUSH b = CreateSolidBrush(T_HOVER);
            FillRect(mem, &r, b); DeleteObject(b);
        }
        const FmItem& it = w->items[i];
        /* ★ 图标按实际边长居中（不是写死的 16），才能和文字对齐。
         *   来源：自带 zywico.dll；自带库没覆盖的类型再去问系统的默认应用关联。 */
        DrawItemIcon(mem, RowIconX(), RowIconY(y), icSz, it);

        COLORREF tc = (it.attr & FILE_ATTRIBUTE_HIDDEN) ? T_FAINT : T_TEXT;
        RECT rn = { RowTextX(icSz), y, cSize - S(6), y + FM_ROW_H };
        DrawTextR(mem, rn, it.name.c_str(), g_fontUI, tc);
        if (!it.isDir || it.isDrive) {
            RECT rs = { cSize, y, cType - S(8), y + FM_ROW_H };
            DrawTextR(mem, rs, SizeText(it.size, false).c_str(), g_fontSmall,
                      T_SUB, DT_RIGHT | DT_SINGLELINE | DT_VCENTER);
        }
        RECT rt = { cType, y, cDate - S(8), y + FM_ROW_H };
        DrawTextR(mem, rt, it.type.c_str(), g_fontSmall, T_SUB);
        RECT rd = { cDate, y, right - S(4), y + FM_ROW_H };
        DrawTextR(mem, rd, DateText(it.mt).c_str(), g_fontSmall, T_FAINT);
    }

    if (n == 0) {
        RECT r = { RowTextX(icSz), listTop + S(24), W - S(30), listTop + S(52) };
        DrawTextR(mem, r, L"\uFF08\u6B64\u6587\u4EF6\u5939\u4E3A\u7A7A\uFF09",
                  g_fontSmall, T_FAINT);
    }
    }   /* end of 详情视图分支 */

    /* ---- 自绘滚动条 ---- */
    if (FmNeedScroll(w)) {
        int sbTop = FmPcView(w) ? (FM_TITLE_H + FM_TOOL_H) : listTop;
        int n, vis;
        FmScrollDims(w, n, vis);
        int x = W - FM_SB_W;
        RECT sr = { x, sbTop, W, statTop };
        HBRUSH b = CreateSolidBrush(T_BG);
        FillRect(mem, &sr, b); DeleteObject(b);

        int trackH = statTop - sbTop;
        int thumbH = trackH * vis / n;
        if (thumbH < S(28)) thumbH = S(28);
        int maxTop = n - vis;
        int ty = sbTop + (maxTop > 0 ? (trackH - thumbH) * w->top / maxTop : 0);
        RECT tb = { x + S(3), ty + S(1), W - S(3), ty + thumbH - S(1) };
        FillRound(mem, tb, FM_RADIUS, T_LINE_STRONG, CLR_INVALID);
    }

    /* ---- 状态栏（浅灰底 + 上边线，同 iced 版底栏） ---- */
    {
        RECT sr = { 0, statTop, W, H };
        HBRUSH b = CreateSolidBrush(T_BG);
        FillRect(mem, &sr, b); DeleteObject(b);
        HLine(mem, 0, W, statTop, T_LINE);

        int nsel = 0;
        for (size_t i = 0; i < w->sel.size(); i++) if (w->sel[i]) nsel++;
        std::wstring s = StrFormat(L"%d \u4E2A\u5BF9\u8C61", (int)w->items.size());
        if (nsel > 0) s += StrFormat(L"\uFF0C\u5DF2\u9009\u4E2D %d \u4E2A", nsel);
        if (!g_clip.empty())
            s += std::wstring(L"    |    \u526A\u8D34\u677F\uFF1A") +
                 StrFormat(L"%d \u9879%s", (int)g_clip.size(), g_clipCut ? L"\uFF08\u526A\u5207\uFF09" : L"");

        /* 左下角：署名（对应 iced 版工具合集侧栏底部那行） */
        RECT rl = { S(14), statTop, W / 2, H };
        DrawTextR(mem, rl, L"\u7AF9\u5F71\u6E05\u98CE\u5236\u4F5C \u00B7 FirTeam\u51FA\u54C1",
                  g_fontSmall, T_FAINT);
        /* 右下角：对象统计 */
        RECT rr2 = { W / 2, statTop, W - S(14), H };
        DrawTextR(mem, rr2, s.c_str(), g_fontSmall, T_SUB,
                  DT_RIGHT | DT_SINGLELINE | DT_VCENTER);
    }

    /* 外边框 */
    HPEN p = CreatePen(PS_SOLID, 1, T_LINE_STRONG);
    HPEN op = (HPEN)SelectObject(mem, p);
    HBRUSH nb = (HBRUSH)SelectObject(mem, GetStockObject(NULL_BRUSH));
    Rectangle(mem, 0, 0, W, H);
    SelectObject(mem, nb);
    SelectObject(mem, op); DeleteObject(p);

    BitBlt(hdc, 0, 0, W, H, mem, 0, 0, SRCCOPY);
    SelectObject(mem, old);
    DeleteObject(dib);
    DeleteDC(mem);
}

/* ============================ 导航 ============================ */
static void FmLayout(FmWin* w);

static void FmSyncEdit(FmWin* w)
{
    if (!w->hEdit) return;
    SetWindowTextW(w->hEdit, w->path.empty() ? L"\u6B64\u7535\u8111" : w->path.c_str());
}

static void FmNavigate(FmWin* w, const std::wstring& path, bool pushHist)
{
    /* 规范化：把 .. / . / 正斜杠 折叠成绝对路径，界面显示更干净 */
    std::wstring norm = path;
    if (!norm.empty()) {
        wchar_t buf[MAX_PATH * 2];
        DWORD n = GetFullPathNameW(norm.c_str(), MAX_PATH * 2, buf, NULL);
        if (n > 0 && n < MAX_PATH * 2) norm = buf;
        while (norm.size() > 3 && norm.back() == L'\\') norm.pop_back();
    }

    if (pushHist) {
        if (w->histPos + 1 < (int)w->hist.size())
            w->hist.erase(w->hist.begin() + w->histPos + 1, w->hist.end());
        w->hist.push_back(norm);
        w->histPos = (int)w->hist.size() - 1;
        if (w->hist.size() > 64) { w->hist.erase(w->hist.begin()); w->histPos--; }
    }
    w->path = norm;
    LoadDir(w);                      /* LoadDir 内部会把 top 归零，不会残留越界值 */
    FmSyncEdit(w);
    InvalidateRect(w->hwnd, NULL, FALSE);
}

static void FmGo(FmWin* w, const std::wstring& p)
{
    if (p.empty() || _wcsicmp(p.c_str(), L"\u6B64\u7535\u8111") == 0 ||
        _wcsicmp(p.c_str(), L"This PC") == 0 || p == L"::") {
        FmNavigate(w, L"", true);
        return;
    }
    std::wstring path = p;
    if (path.size() == 2 && path[1] == L':') path.push_back(L'\\');   /* "C:" → "C:\" */
    while (path.size() > 3 && path.back() == L'\\') path.pop_back();

    /* 相对路径按「当前目录」解释（和 Explorer 地址栏一致），例如当前在 D:\Tools
     * 时输入 PE 就跳到 D:\Tools\PE */
    bool absolute = (path.size() >= 2 && path[1] == L':') ||
                    (path.size() >= 2 && path[0] == L'\\' && path[1] == L'\\');
    if (!absolute && !w->path.empty()) path = JoinPath(w->path, path);

    /* ★ BUG-2：原来用 FileExistsW 判断，而它**明确排除目录**
     *   （util.cpp 里 !(a & FILE_ATTRIBUTE_DIRECTORY)），于是输入 D:\ 或 D:\Tools
     *   一律被判成「路径不存在」—— 偏偏跳目录才是地址栏最主要的用法。
     *   这里改用 GetFileAttributesW 自己判：目录→导航，文件→直接运行。 */
    DWORD attr = GetFileAttributesW(path.c_str());
    if (attr == INVALID_FILE_ATTRIBUTES) {
        MessageBoxW(w->hwnd, (L"\u8DEF\u5F84\u4E0D\u5B58\u5728\uFF1A\n\n" + path).c_str(),
                    APP_TITLE, MB_OK | MB_ICONWARNING);
        FmSyncEdit(w);
        return;
    }
    if (!(attr & FILE_ATTRIBUTE_DIRECTORY)) {
        RunCommand(L"\"" + path + L"\"");           /* 是文件：交给系统打开 */
        FmSyncEdit(w);
        return;
    }
    FmNavigate(w, path, true);
}

static void FmOpenItem(FmWin* w, int idx)
{
    if (idx < 0 || idx >= (int)w->items.size()) return;
    const FmItem& it = w->items[idx];
    if (it.isDir) { FmNavigate(w, it.full, true); return; }
    RunCommand(L"\"" + it.full + L"\"");
}

static void FmRunOnSel(FmWin* w, const wchar_t* verb, const wchar_t* prog)
{
    int n = 0;
    for (size_t i = 0; i < w->items.size(); i++) {
        if (!w->sel[i] || w->items[i].isDrive) continue;   /* BUG-5：跳过驱动器 */
        n++;
        std::wstring cmd;
        if (prog) cmd = L"\"" + std::wstring(prog) + L"\" \"" + w->items[i].full + L"\"";
        else      cmd = L"\"" + w->items[i].full + L"\"";
        (void)verb;
        RunCommand(cmd);
    }
    if (n == 0) MessageBoxW(w->hwnd, L"\u8BF7\u5148\u9009\u4E2D\u6587\u4EF6\u3002", APP_TITLE,
                            MB_OK | MB_ICONINFORMATION);
}

static void FmSelectAll(FmWin* w)
{
    for (size_t i = 0; i < w->sel.size(); i++) w->sel[i] = 1;
    InvalidateRect(w->hwnd, NULL, FALSE);
}

static void FmCopyToClip(FmWin* w, bool cut)
{
    /* ★ BUG-5：首页「此电脑」的磁贴也是 FmItem，若不按 isDrive 过滤，
     *   选中 C: 后 Ctrl+C、再到别处 Ctrl+V 就会触发**整个盘**的复制；
     *   剪切更糟（FO_MOVE 对盘符）。这里直接把驱动器排除掉。 */
    g_clip.clear();
    int skipped = 0;
    for (size_t i = 0; i < w->items.size(); i++) {
        if (!w->sel[i]) continue;
        if (w->items[i].isDrive) { skipped++; continue; }
        g_clip.push_back(w->items[i].full);
    }
    g_clipCut = cut;
    if (skipped > 0)
        MessageBoxW(w->hwnd, L"\u9A71\u52A8\u5668\u4E0D\u80FD\u590D\u5236\u6216\u526A\u5207\uFF0C\u5DF2\u8DF3\u8FC7\u3002",
                    APP_TITLE, MB_OK | MB_ICONINFORMATION);
    InvalidateRect(w->hwnd, NULL, FALSE);
}

/* 用 SHFileOperation 完成复制 / 移动 / 删除 */
static bool ShellOp(HWND owner, UINT op, const std::vector<std::wstring>& src,
                    const std::wstring& dst, DWORD flags)
{
    if (src.empty()) return false;
    std::wstring from;
    for (size_t i = 0; i < src.size(); i++) { from += src[i]; from.push_back(L'\0'); }
    from.push_back(L'\0');
    std::wstring to = dst;
    if (!to.empty()) to.push_back(L'\0');

    SHFILEOPSTRUCTW fos = { 0 };
    fos.hwnd   = owner;
    fos.wFunc  = op;
    fos.pFrom  = from.c_str();
    fos.pTo    = to.empty() ? NULL : to.c_str();
    fos.fFlags = (WORD)flags;
    return SHFileOperationW(&fos) == 0;
}

static void FmPaste(FmWin* w)
{
    if (g_clip.empty() || w->path.empty()) return;
    DWORD flags = FOF_NOCONFIRMATION | FOF_NOCONFIRMMKDIR | FOF_NOERRORUI | FOF_SILENT;
    bool ok = ShellOp(w->hwnd, g_clipCut ? FO_MOVE : FO_COPY, g_clip, w->path, flags);
    if (ok && g_clipCut) { g_clip.clear(); g_clipCut = false; }
    LoadDir(w);
    FmSyncEdit(w);
    InvalidateRect(w->hwnd, NULL, FALSE);
}

/* 该卷上有没有可用的回收站？—— WinPE / X: 盘 / FAT 分区上通常没有。
 * 没有回收站时再传 FOF_ALLOWUNDO 是没意义的，还会让用户误以为能还原，
 * 所以这种情况直接按「永久删除」处理，并把提示说清楚。 */
static bool VolumeHasRecycleBin(const std::wstring& path)
{
    if (path.size() < 2 || path[1] != L':') return false;
    std::wstring root = path.substr(0, 2) + L"\\$Recycle.Bin";
    DWORD a = GetFileAttributesW(root.c_str());
    return a != INVALID_FILE_ATTRIBUTES && (a & FILE_ATTRIBUTE_DIRECTORY) != 0;
}

static void FmDelete(FmWin* w, bool permanent)
{
    /* ★ BUG-5：同上，绝不把驱动器送进 SHFileOperation 的 FO_DELETE ——
     *   PE 下没有回收站，那是真·整盘删除。 */
    std::vector<std::wstring> sel;
    int skipped = 0;
    for (size_t i = 0; i < w->items.size(); i++) {
        if (!w->sel[i]) continue;
        if (w->items[i].isDrive) { skipped++; continue; }
        sel.push_back(w->items[i].full);
    }
    if (sel.empty()) {
        if (skipped > 0)
            MessageBoxW(w->hwnd, L"\u9A71\u52A8\u5668\u4E0D\u80FD\u5220\u9664\u3002",
                        APP_TITLE, MB_OK | MB_ICONINFORMATION);
        return;
    }

    bool canUndo = !permanent && VolumeHasRecycleBin(sel[0]);
    std::wstring msg = StrFormat(L"\u786E\u5B9A\u5220\u9664\u9009\u4E2D\u7684 %d \u4E2A\u5BF9\u8C61\uFF1F",
                                 (int)sel.size());
    if (!canUndo) msg += L"\n\n\uFF08\u6B64\u4F4D\u7F6E\u6CA1\u6709\u56DE\u6536\u7AD9\uFF0C\u5220\u9664\u540E\u4E0D\u53EF\u6062\u590D\uFF09";
    if (MessageBoxW(w->hwnd, msg.c_str(), APP_TITLE,
                    MB_YESNO | MB_ICONQUESTION | MB_DEFBUTTON2) != IDYES) return;

    DWORD flags = FOF_NOCONFIRMATION | FOF_NOERRORUI | FOF_SILENT;
    if (canUndo) flags |= FOF_ALLOWUNDO;
    ShellOp(w->hwnd, FO_DELETE, sel, L"", flags);
    LoadDir(w);
    FmSyncEdit(w);
    InvalidateRect(w->hwnd, NULL, FALSE);
}

static void FmNewFolder(FmWin* w)
{
    if (w->path.empty()) return;

    /* 弹出自绘输入框命名（默认「新建文件夹」，重名自动加序号） */
    std::wstring name = L"\u65B0\u5EFA\u6587\u4EF6\u5939";
    for (int k = 2; k < 1000; k++) {
        if (!FileExistsW(JoinPath(w->path, name).c_str())) break;
        name = StrFormat(L"\u65B0\u5EFA\u6587\u4EF6\u5939 (%d)", k);
    }
    if (ZywInputBox(w->hwnd, L"\u65B0\u5EFA\u6587\u4EF6\u5939",
                    L"\u8BF7\u8F93\u5165\u65B0\u6587\u4EF6\u5939\u540D\u79F0\uFF1A", name) != 0)
        return;
    name = TrimStr(name);
    if (name.empty()) return;
    /* 去掉非法字符 */
    for (size_t i = 0; i < name.size(); i++)
        if (name[i] == L'\\' || name[i] == L'/' || name[i] == L':' ||
            name[i] == L'*' || name[i] == L'?' || name[i] == L'"' ||
            name[i] == L'<' || name[i] == L'>' || name[i] == L'|') name[i] = L'_';
    if (name.empty()) return;

    std::wstring full = JoinPath(w->path, name);
    if (!CreateDirectoryW(full.c_str(), NULL)) {
        MessageBoxW(w->hwnd, (L"\u65E0\u6CD5\u521B\u5EFA\uFF1A\n" + full).c_str(),
                    APP_TITLE, MB_OK | MB_ICONWARNING);
        return;
    }
    LoadDir(w);
    for (size_t i = 0; i < w->items.size(); i++)
        if (_wcsicmp(w->items[i].name.c_str(), name.c_str()) == 0) {
            for (size_t j = 0; j < w->sel.size(); j++) w->sel[j] = 0;
            w->sel[i] = 1;
            w->anchor = (int)i;
        }
    InvalidateRect(w->hwnd, NULL, FALSE);
}

static void FmRename(FmWin* w)
{
    int idx = -1;
    for (size_t i = 0; i < w->items.size(); i++) if (w->sel[i]) { idx = (int)i; break; }
    if (idx < 0) return;
    const FmItem& it = w->items[idx];
    /* BUG-5：对 C:\ 执行 MoveFileW 会把盘符名改掉，必须挡住 */
    if (it.isDrive) {
        MessageBoxW(w->hwnd, L"\u9A71\u52A8\u5668\u4E0D\u80FD\u91CD\u547D\u540D\u3002",
                    APP_TITLE, MB_OK | MB_ICONINFORMATION);
        return;
    }

    /* 用一个自绘小输入框取新名称 */
    std::wstring newName = it.name;
    if (ZywInputBox(w->hwnd, L"\u91CD\u547D\u540D",
                    L"\u8BF7\u8F93\u5165\u65B0\u540D\u79F0\uFF1A", newName) != 0)
        return;
    if (newName.empty() || newName == it.name) return;
    if (newName.find(L'\\') != std::wstring::npos || newName.find(L'/') != std::wstring::npos) {
        MessageBoxW(w->hwnd, L"\u540D\u79F0\u4E0D\u80FD\u5305\u542B \\ \u6216 /", APP_TITLE, MB_OK | MB_ICONWARNING);
        return;
    }
    std::wstring dst = JoinPath(w->path, newName);
    if (FileExistsW(dst.c_str())) {
        MessageBoxW(w->hwnd, L"\u540C\u540D\u6587\u4EF6\u5DF2\u5B58\u5728\u3002", APP_TITLE, MB_OK | MB_ICONWARNING);
        return;
    }
    if (!MoveFileW(it.full.c_str(), dst.c_str()))
        MessageBoxW(w->hwnd, L"\u91CD\u547D\u540D\u5931\u8D25\uFF08\u53EF\u80FD\u662F\u53EA\u8BFB\u6216\u88AB\u5360\u7528\uFF09\u3002",
                    APP_TITLE, MB_OK | MB_ICONWARNING);
    LoadDir(w);
    InvalidateRect(w->hwnd, NULL, FALSE);
}

static void FmProperties(FmWin* w)
{
    for (size_t i = 0; i < w->items.size(); i++) {
        if (!w->sel[i]) continue;
        SHELLEXECUTEINFOW sei = { sizeof(sei) };
        sei.fMask  = SEE_MASK_INVOKEIDLIST;
        sei.hwnd   = w->hwnd;
        sei.lpVerb = L"properties";
        sei.lpFile = w->items[i].full.c_str();
        sei.nShow  = SW_SHOW;
        ShellExecuteExW(&sei);
        break;
    }
}

/* ============================ 右键菜单 ============================ */
enum {
    CM_OPEN = 1, CM_OPEN_NP, CM_COPY, CM_CUT, CM_PASTE, CM_DEL, CM_REN,
    CM_NEWDIR, CM_PROP, CM_REFRESH, CM_SELALL, CM_DELPERM
};

static void FmContextMenu(FmWin* w, int x, int y)
{
    bool hasSel = false;
    /* hasOp = 选中的**非驱动器**项（BUG-5：复制/剪切/删除/重命名只对这些可用；
     * 打开和属性对驱动器仍然有效） */
    bool hasOp  = false;
    for (size_t i = 0; i < w->sel.size() && i < w->items.size(); i++) {
        if (!w->sel[i]) continue;
        hasSel = true;
        if (!w->items[i].isDrive) hasOp = true;
    }

    HMENU mn = CreatePopupMenu();
    AppendMenuW(mn, MF_STRING | (hasSel ? 0 : MF_GRAYED), CM_OPEN, L"\u6253\u5F00(&O)");
    AppendMenuW(mn, MF_STRING | (hasOp  ? 0 : MF_GRAYED), CM_OPEN_NP,
                L"\u7528\u8BB0\u4E8B\u672C\u6253\u5F00(&N)");
    AppendMenuW(mn, MF_SEPARATOR, 0, NULL);
    AppendMenuW(mn, MF_STRING | (hasOp  ? 0 : MF_GRAYED), CM_COPY, L"\u590D\u5236(&C)");
    AppendMenuW(mn, MF_STRING | (hasOp  ? 0 : MF_GRAYED), CM_CUT,  L"\u526A\u5207(&X)");
    AppendMenuW(mn, MF_STRING | (g_clip.empty() || w->path.empty() ? MF_GRAYED : 0),
                CM_PASTE, L"\u7C98\u8D34(&V)");
    AppendMenuW(mn, MF_SEPARATOR, 0, NULL);
    AppendMenuW(mn, MF_STRING | (hasOp  ? 0 : MF_GRAYED), CM_REN, L"\u91CD\u547D\u540D(&R)");
    AppendMenuW(mn, MF_STRING | (hasOp  ? 0 : MF_GRAYED), CM_DEL, L"\u5220\u9664(&D)");
    AppendMenuW(mn, MF_STRING | (hasOp  ? 0 : MF_GRAYED), CM_DELPERM, L"\u5F7B\u5E95\u5220\u9664");
    AppendMenuW(mn, MF_SEPARATOR, 0, NULL);
    AppendMenuW(mn, MF_STRING | (w->path.empty() ? MF_GRAYED : 0), CM_NEWDIR,
                L"\u65B0\u5EFA\u6587\u4EF6\u5939(&F)");
    AppendMenuW(mn, MF_STRING, CM_REFRESH, L"\u5237\u65B0(&E)");
    AppendMenuW(mn, MF_STRING, CM_SELALL, L"\u5168\u9009(&A)");
    AppendMenuW(mn, MF_SEPARATOR, 0, NULL);
    AppendMenuW(mn, MF_STRING | (hasSel ? 0 : MF_GRAYED), CM_PROP, L"\u5C5E\u6027(&P)");

    POINT pt = { x, y };
    ClientToScreen(w->hwnd, &pt);
    int c = TrackPopupMenu(mn, TPM_RETURNCMD | TPM_LEFTALIGN | TPM_TOPALIGN,
                           pt.x, pt.y, 0, w->hwnd, NULL);
    DestroyMenu(mn);

    switch (c) {
    case CM_OPEN:    for (size_t i = 0; i < w->items.size(); i++) if (w->sel[i]) { FmOpenItem(w, (int)i); break; } break;
    case CM_OPEN_NP: FmRunOnSel(w, L"open", (GetSysDir() + L"\\notepad.exe").c_str()); break;
    case CM_COPY:    FmCopyToClip(w, false); break;
    case CM_CUT:     FmCopyToClip(w, true);  break;
    case CM_PASTE:   FmPaste(w); break;
    case CM_DEL:     FmDelete(w, false); break;
    case CM_DELPERM: FmDelete(w, true);  break;
    case CM_REN:     FmRename(w); break;
    case CM_NEWDIR:  FmNewFolder(w); break;
    case CM_PROP:    FmProperties(w); break;
    case CM_REFRESH: LoadDir(w); InvalidateRect(w->hwnd, NULL, FALSE); break;
    case CM_SELALL:  FmSelectAll(w); break;
    }
}

/* ============================ 布局同步 ============================ */
static void FmLayout(FmWin* w)
{
    if (!w->hEdit) return;
    RECT cr; GetClientRect(w->hwnd, &cr);
    RECT a = FmAddrRect(cr.right);
    MoveWindow(w->hEdit, a.left + S(6), a.top + S(3),
               a.right - a.left - S(12), a.bottom - a.top - S(6), TRUE);
}

/* ============================ 窗口过程 ============================ */
static FmWin* FmFrom(HWND h) { return (FmWin*)GetWindowLongPtrW(h, GWLP_USERDATA); }

/* ★ BUG-1 修复：地址栏回车。
 * 单行 EDIT 按回车**不会**给父窗口发 WM_COMMAND（EDIT 的通知码是 0x0100/0x0200/…，
 * 不存在 0），VK_RETURN 也被 EDIT 自己吃掉 —— 所以原来写在 WM_COMMAND 里
 * 「HIWORD(wp)==0」的分支是永远不会进的死代码（已用 check\entertest 实测坐实）。
 * 正确做法：给地址栏做子类化，在子类里直接拦 WM_KEYDOWN + VK_RETURN。 */
static WNDPROC g_editOldProc = NULL;

static LRESULT CALLBACK FmEditProc(HWND he, UINT m, WPARAM wp, LPARAM lp)
{
    FmWin* w = (FmWin*)GetWindowLongPtrW(he, GWLP_USERDATA);
    if (w && m == WM_KEYDOWN && wp == VK_RETURN) {
        wchar_t buf[MAX_PATH * 2] = { 0 };
        GetWindowTextW(he, buf, MAX_PATH * 2);
        FmGo(w, TrimStr(buf));
        return 0;
    }
    if (w && m == WM_KEYDOWN && wp == VK_ESCAPE) {
        FmSyncEdit(w);                 /* 放弃编辑，地址栏还原成当前路径 */
        SetFocus(w->hwnd);
        return 0;
    }
    return CallWindowProcW(g_editOldProc, he, m, wp, lp);
}

LRESULT CALLBACK Fm_WndProc(HWND h, UINT m, WPARAM wp, LPARAM lp)
{
    FmWin* w = FmFrom(h);
    if (!w && m != WM_CREATE && m != WM_NCCREATE) return DefWindowProcW(h, m, wp, lp);

    switch (m) {
    case WM_NCCALCSIZE:
        return 0;                                    /* 客户区 = 整个窗口 */

    case WM_NCHITTEST: {
        POINT pt = { GET_X_LPARAM(lp), GET_Y_LPARAM(lp) };
        RECT wr; GetWindowRect(h, &wr);
        /* 最大化时不做边缘缩放（否则拖动边缘会把窗口从最大化状态拉下来） */
        if (!IsZoomed(h)) {
            int b = 6;
            bool L = pt.x < wr.left + b, R = pt.x >= wr.right - b;
            bool T = pt.y < wr.top + b,  B = pt.y >= wr.bottom - b;
            if (T && L) return HTTOPLEFT;
            if (T && R) return HTTOPRIGHT;
            if (B && L) return HTBOTTOMLEFT;
            if (B && R) return HTBOTTOMRIGHT;
            if (L) return HTLEFT;
            if (R) return HTRIGHT;
            if (T) return HTTOP;
            if (B) return HTBOTTOM;
        }
        POINT cp = pt; ScreenToClient(h, &cp);
        if (cp.y < FM_TITLE_H) {
            RECT cr; GetClientRect(h, &cr);
            /* 窗口按钮交给 WM_LBUTTONDOWN 自己处理（自绘窗口用 HTCAPTION 更稳） */
            if (FmTitleBtnAt(cr.right, cp.x, cp.y) >= 0) return HTCLIENT;
            return HTCAPTION;                                 /* 其余整条标题栏可拖动 */
        }
        return HTCLIENT;
    }

    /* 双击标题栏 = 最大化 / 还原（Windows 通用习惯） */
    case WM_NCLBUTTONDBLCLK:
        if (wp == HTCAPTION) {
            ShowWindow(h, IsZoomed(h) ? SW_RESTORE : SW_MAXIMIZE);
            return 0;
        }
        break;

    case WM_GETMINMAXINFO: {
        MINMAXINFO* mm = (MINMAXINFO*)lp;
        RECT wa; SystemParametersInfoW(SPI_GETWORKAREA, 0, &wa, 0);
        mm->ptMaxPosition.x = wa.left;
        mm->ptMaxPosition.y = wa.top;
        mm->ptMaxSize.x = wa.right - wa.left;
        mm->ptMaxSize.y = wa.bottom - wa.top;
        mm->ptMinTrackSize.x = S(720);
        mm->ptMinTrackSize.y = S(460);
        return 0;
    }

    case WM_CREATE: {
        /* ★★ 千万不要在这里 ZeroMemory(w, sizeof(FmWin))！★★
         * FmWin 里有 std::wstring path / std::vector items / sel / hist，
         * new FmWin() 已经把 POD 成员清零、并把这些 std 成员**正常构造**好了；
         * 再整块 ZeroMemory 会把 std::wstring 的 _Myres（容量）打成 0。
         * 之后第一次给它赋值时，MSVC 的 basic_string 会「认为需要重新分配」→
         * 把字符写进**堆缓冲**，同时把 _Myres 设成 SSO 容量（7），于是
         * _Large_string_engaged() 判定为「还在内联缓冲里」→ c_str() 返回的
         * 其实是 _Bx._Buf 里被 _Ptr 覆盖掉的那几个字节 —— 也就是**堆指针本身的
         * 字节**。表现：首页第一次点盘，地址栏出现 3 个乱码字（长度恰好等于
         * "C:\"），进入一个不存在的空目录；再点一次又正常了（_Myres 已被修正）。
         * 教训：对含 C++ 对象的结构体做 new + memset/ZeroMemory 是 UB。 */
        w = new FmWin();                 /* 值初始化：POD 清零 + std 成员正常构造 */
        w->hwnd        = h;
        w->hEdit       = NULL;
        w->histPos     = -1;
        w->anchor      = -1;
        w->hover       = -1;
        w->top         = 0;
        w->hovBtn      = -1;
        w->hovDrive    = -1;
        w->hovTitleBtn = -1;
        w->tracking    = false;
        w->dragSb      = false;
        w->dragY0      = 0;
        w->dragTop0    = 0;
        w->sortBy      = SORT_NAME;
        w->sortAsc     = true;
        SetWindowLongPtrW(h, GWLP_USERDATA, (LONG_PTR)w);
        g_fms.push_back(w);

        w->hEdit = CreateWindowExW(0, L"EDIT", L"", WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL,
                                   0, 0, 10, 10, h, (HMENU)1001, g_hInst, NULL);
        if (w->hEdit) {
            if (g_fontUI) SendMessageW(w->hEdit, WM_SETFONT, (WPARAM)g_fontUI, TRUE);
            /* BUG-1：子类化，用于接收回车 / Esc（见 FmEditProc 注释） */
            SetWindowLongPtrW(w->hEdit, GWLP_USERDATA, (LONG_PTR)w);
            g_editOldProc = (WNDPROC)SetWindowLongPtrW(w->hEdit, GWLP_WNDPROC,
                                                       (LONG_PTR)FmEditProc);
        }
        return 0;
    }

    case WM_SIZE:
        if (!w) break;
        FmLayout(w);
        /* ★ BUG-4：窗口变小后可视行数变少，但 top 没被夹紧 → 首页所有磁贴的
         *   「行号 - top」都成负数、详情视图行循环直接不画，右栏一片空白。
         *   这里在布局后把 top 夹到新的 maxTop。（放大不需要夹，top 本来就合法） */
        {
            int n, vis;
            FmScrollDims(w, n, vis);
            int maxTop = n - vis; if (maxTop < 0) maxTop = 0;
            if (w->top > maxTop) w->top = maxTop;
        }
        InvalidateRect(h, NULL, FALSE);
        return 0;

    case WM_ERASEBKGND:
        return 1;

    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC dc = BeginPaint(h, &ps);
        FmPaint(w, dc);
        EndPaint(h, &ps);
        return 0;
    }

    case WM_CTLCOLOREDIT:
    case WM_CTLCOLORSTATIC: {
        HDC dc = (HDC)wp;
        SetTextColor(dc, T_TEXT);
        SetBkColor(dc, T_CARD);
        static HBRUSH s_br = NULL;
        if (!s_br) s_br = CreateSolidBrush(T_CARD);
        return (LRESULT)s_br;
    }

    case WM_COMMAND:
        /* ★ BUG-1：回车不再走这里（EDIT 从不发 notify=0 的 WM_COMMAND），
         *   改由 FmEditProc 子类处理；这里只剩「编辑中不要触发别的东西」。 */
        if (LOWORD(wp) == 1001) return 0;
        return 0;

    case WM_KEYDOWN:
        if (GetFocus() == w->hEdit && wp != VK_RETURN && wp != VK_ESCAPE) break;
        switch (wp) {
        case VK_RETURN: {
            for (size_t i = 0; i < w->items.size(); i++)
                if (w->sel[i]) { FmOpenItem(w, (int)i); break; }
            break;
        }
        case VK_BACK:
            if (!w->path.empty()) FmNavigate(w, ParentOf(w->path), true);
            break;
        case VK_DELETE: FmDelete(w, (GetKeyState(VK_SHIFT) & 0x8000) != 0); break;
        case VK_F2:     FmRename(w); break;
        case VK_F5:     LoadDir(w); InvalidateRect(h, NULL, FALSE); break;
        case VK_ESCAPE:
            if (IsIconic(h)) ShowWindow(h, SW_RESTORE);
            break;
        case 'A': if (GetKeyState(VK_CONTROL) & 0x8000) FmSelectAll(w); break;
        case 'C': if (GetKeyState(VK_CONTROL) & 0x8000) FmCopyToClip(w, false); break;
        case 'X': if (GetKeyState(VK_CONTROL) & 0x8000) FmCopyToClip(w, true);  break;
        case 'V': if (GetKeyState(VK_CONTROL) & 0x8000) FmPaste(w); break;
        case VK_UP:
        case VK_DOWN: {
            int n = (int)w->items.size();
            if (n == 0) break;
            int cur = -1;
            for (int i = 0; i < n; i++) if (w->sel[i]) { cur = i; break; }
            int nx = (wp == VK_DOWN) ? (cur + 1) : (cur < 0 ? 0 : cur - 1);
            if (cur < 0) nx = 0;
            if (nx < 0) nx = 0;
            if (nx >= n) nx = n - 1;
            bool ctrl = (GetKeyState(VK_CONTROL) & 0x8000) != 0;
            bool shift = (GetKeyState(VK_SHIFT) & 0x8000) != 0;
            if (!ctrl && !shift) for (int i = 0; i < n; i++) w->sel[i] = 0;
            if (shift && w->anchor >= 0) {
                int a = w->anchor, b2 = nx;
                if (a > b2) { int t = a; a = b2; b2 = t; }
                for (int i = a; i <= b2; i++) w->sel[i] = 1;
            } else {
                w->sel[nx] = 1;
                w->anchor = nx;
            }
            FmEnsureVisible(w, nx);
            InvalidateRect(h, NULL, FALSE);
            break;
        }
        }
        return 0;

    case WM_MOUSEMOVE: {
        int x = GET_X_LPARAM(lp), y = GET_Y_LPARAM(lp);
        RECT cr; GetClientRect(h, &cr);
        int statTop = FmStatusTop(h);

        if (w->dragSb) {
            /* ★ BUG-3：这里原来是详情视图的算法（items.size() / FmRowsVisible /
             *   FmListTop），首页网格下算出的 maxTop 恒 ≤ 0，滑块根本拖不动。
             *   现在和 WM_LBUTTONDOWN 里那段统一走 FmScrollDims + sbTop。 */
            int n, vis;
            FmScrollDims(w, n, vis);
            int maxTop = n - vis;
            if (maxTop > 0 && vis > 0) {
                int sbTop = FmPcView(w) ? (FM_TITLE_H + FM_TOOL_H) : FmListTop();
                int trackH = statTop - sbTop;
                int thumbH = trackH * vis / n; if (thumbH < S(28)) thumbH = S(28);
                int span = trackH - thumbH; if (span < 1) span = 1;
                int d = y - w->dragY0;
                int nt = w->dragTop0 + d * maxTop / span;
                if (nt < 0) nt = 0;
                if (nt > maxTop) nt = maxTop;
                if (nt != w->top) { w->top = nt; InvalidateRect(h, NULL, FALSE); }
            }
            return 0;
        }

        if (!w->tracking) {
            TRACKMOUSEEVENT tme = { sizeof(tme), TME_LEAVE, h, 0 };
            TrackMouseEvent(&tme);
            w->tracking = true;
        }

        int nt = FmTitleBtnAt(cr.right, x, y);

        int nb = -1;
        for (int i = 0; i < FB_COUNT; i++) {
            RECT b = FmBtnRect(i);
            if (x >= b.left && x < b.right && y >= b.top && y < b.bottom) nb = i;
        }
        int nd = -2;
        if (x < FM_LEFTW && y > FM_TITLE_H + FM_TOOL_H && y < statTop) {
            nd = -1;
            int y0 = FmSideItemTop();
            if (y >= y0 && y < y0 + FM_SIDE_ROW_V) nd = -2;
            else {
                int dy = FmSideDriveTop(), k = 0;
                DWORD drives = GetLogicalDrives();
                for (int i = 0; i < 26; i++) {
                    if (!(drives & (1u << i))) continue;
                    wchar_t root[4] = { (wchar_t)(L'A' + i), L':', L'\\', 0 };
                    UINT t = GetDriveTypeW(root);
                    if (t == DRIVE_NO_ROOT_DIR || t == DRIVE_UNKNOWN) continue;
                    if (y >= dy && y < dy + FM_SIDE_ROW_V) { nd = k; break; }
                    dy += FM_SIDE_ROW_H; k++;
                }
                if (nd == -1) nd = -99;
            }
        }
        int nh = -1;
        if (x > FM_LEFTW && y >= (FmPcView(w) ? (FM_TITLE_H + FM_TOOL_H) : FmListTop())
            && y < statTop) {
            if (FmPcView(w)) {
                nh = FmTileAt(h, w, x, y);
            } else {
                int i = w->top + (y - FmListTop()) / FM_ROW_H;
                if (i >= 0 && i < (int)w->items.size()) nh = i;
            }
        }
        if (nb != w->hovBtn || nd != w->hovDrive || nh != w->hover || nt != w->hovTitleBtn) {
            w->hovBtn = nb; w->hovDrive = nd; w->hover = nh; w->hovTitleBtn = nt;
            InvalidateRect(h, NULL, FALSE);
        }
        return 0;
    }

    case WM_MOUSELEAVE:
        w->tracking = false;
        w->hovBtn = -1; w->hovDrive = -99; w->hover = -1; w->hovTitleBtn = -1;
        InvalidateRect(h, NULL, FALSE);
        return 0;

    case WM_LBUTTONDOWN: {
        int x = GET_X_LPARAM(lp), y = GET_Y_LPARAM(lp);
        SetFocus(h);
        RECT cr; GetClientRect(h, &cr);
        int statTop = FmStatusTop(h);

        /* 标题栏窗口按钮：— 最小化   □ 最大化/还原   × 关闭 */
        {
            int bi = FmTitleBtnAt(cr.right, x, y);
            if (bi == TB_MIN)   { ShowWindow(h, SW_MINIMIZE); return 0; }
            if (bi == TB_MAX)   { ShowWindow(h, IsZoomed(h) ? SW_RESTORE : SW_MAXIMIZE); return 0; }
            if (bi == TB_CLOSE) { PostMessageW(h, WM_CLOSE, 0, 0); return 0; }
        }

        /* 工具栏按钮：后退 / 前进 / 上级 / 刷新 / 新建文件夹
         * ★ 9.3 修 bug：这 5 个按钮以前**只画不响应** —— FmPaint 里画了，
         *   但 WM_LBUTTONDOWN 里压根没有工具栏分支，所以点哪个都没反应
         *   （和标题栏按钮是同一类 bug）。禁用态与 FmPaint 里的 enabled 判定保持一致。 */
        {
            int bi = FmToolBtnAt(x, y);
            if (bi >= 0) {
                switch (bi) {
                case FB_BACK:
                    if (w->histPos > 0) {
                        w->histPos--;
                        std::wstring t = w->hist[w->histPos];   /* 先拷出来：FmNavigate 会动 hist */
                        FmNavigate(w, t, false);
                    }
                    break;
                case FB_FWD:
                    if (w->histPos + 1 < (int)w->hist.size()) {
                        w->histPos++;
                        std::wstring t = w->hist[w->histPos];
                        FmNavigate(w, t, false);
                    }
                    break;
                case FB_UP:
                    if (!w->path.empty()) FmNavigate(w, ParentOf(w->path), true);
                    break;
                case FB_REFRESH:
                    LoadDir(w);
                    InvalidateRect(h, NULL, FALSE);
                    break;
                case FB_NEWDIR:
                    if (!w->path.empty()) FmNewFolder(w);
                    break;
                }
                return 0;
            }
        }

        {
            int sbTop = FmPcView(w) ? (FM_TITLE_H + FM_TOOL_H) : FmListTop();
            if (FmNeedScroll(w) && x >= cr.right - FM_SB_W && y >= sbTop && y < statTop) {
                int n, vis;
                FmScrollDims(w, n, vis);
                int trackH = statTop - sbTop;
                int thumbH = trackH * vis / n; if (thumbH < S(28)) thumbH = S(28);
                int maxTop = n - vis;
                int ty = sbTop + (maxTop > 0 ? (trackH - thumbH) * w->top / maxTop : 0);
                if (y < ty) { w->top -= vis; if (w->top < 0) w->top = 0; InvalidateRect(h, NULL, FALSE); }
                else if (y > ty + thumbH) { w->top += vis; if (w->top > maxTop) w->top = maxTop; InvalidateRect(h, NULL, FALSE); }
                else { w->dragSb = true; w->dragY0 = y; w->dragTop0 = w->top; SetCapture(h); }
                return 0;
            }
        }

        /* 左栏盘符 */
        if (x < FM_LEFTW && y > FM_TITLE_H + FM_TOOL_H && y < statTop) {
            int y0 = FmSideItemTop();
            if (y >= y0 && y < y0 + FM_SIDE_ROW_V) { FmNavigate(w, L"", true); return 0; }
            int dy = FmSideDriveTop();
            DWORD drives = GetLogicalDrives();
            for (int i = 0; i < 26; i++) {
                if (!(drives & (1u << i))) continue;
                wchar_t root[4] = { (wchar_t)(L'A' + i), L':', L'\\', 0 };
                UINT t = GetDriveTypeW(root);
                if (t == DRIVE_NO_ROOT_DIR || t == DRIVE_UNKNOWN) continue;
                if (y >= dy && y < dy + FM_SIDE_ROW_V) {
                    FmNavigate(w, std::wstring(root), true);
                    return 0;
                }
                dy += FM_SIDE_ROW_H;
            }
            return 0;
        }

        /* 列头排序（「此电脑」网格视图没有列头，跳过） */
        if (FmPcView(w)) {
            /* 网格区域：点击 = 选中磁贴 */
            if (x > FM_LEFTW && y < statTop) {
                int i = FmTileAt(h, w, x, y);
                if (i >= 0) {
                    bool ctrl = (GetKeyState(VK_CONTROL) & 0x8000) != 0;
                    bool shift = (GetKeyState(VK_SHIFT) & 0x8000) != 0;
                    if (shift && w->anchor >= 0) {
                        for (size_t j = 0; j < w->sel.size(); j++) w->sel[j] = 0;
                        int a = w->anchor, b2 = i;
                        if (a > b2) { int t = a; a = b2; b2 = t; }
                        for (int j = a; j <= b2; j++) w->sel[j] = 1;
                    } else if (ctrl) {
                        w->sel[i] = w->sel[i] ? 0 : 1;
                        w->anchor = i;
                    } else {
                        for (size_t j = 0; j < w->sel.size(); j++) w->sel[j] = 0;
                        w->sel[i] = 1;
                        w->anchor = i;
                    }
                }
                InvalidateRect(h, NULL, FALSE);
            }
            return 0;
        }

        /* 列头排序 */
        if (y >= FM_TITLE_H + FM_TOOL_H && y < FmListTop() && x > FM_LEFTW) {
            int right = cr.right - (FmNeedScroll(w) ? FM_SB_W : 0) - S(6);
            int cDate = right - S(132), cType = cDate - S(152), cSize = cType - S(92);
            if (cSize < FM_LEFTW + S(200)) cSize = FM_LEFTW + S(200);
            int id = SORT_NAME;
            if (x >= cDate) id = SORT_DATE;
            else if (x >= cType) id = SORT_TYPE;
            else if (x >= cSize) id = SORT_SIZE;
            if (id == w->sortBy) w->sortAsc = !w->sortAsc;
            else { w->sortBy = id; w->sortAsc = true; }
            SortItems(w);
            w->top = 0;
            InvalidateRect(h, NULL, FALSE);
            return 0;
        }

        /* 文件行 */
        if (x > FM_LEFTW && y >= FmListTop() && y < statTop) {
            int i = w->top + (y - FmListTop()) / FM_ROW_H;
            if (i >= 0 && i < (int)w->items.size()) {
                bool ctrl = (GetKeyState(VK_CONTROL) & 0x8000) != 0;
                bool shift = (GetKeyState(VK_SHIFT) & 0x8000) != 0;
                if (shift && w->anchor >= 0) {
                    for (size_t j = 0; j < w->sel.size(); j++) w->sel[j] = 0;
                    int a = w->anchor, b2 = i;
                    if (a > b2) { int t = a; a = b2; b2 = t; }
                    for (int j = a; j <= b2; j++) w->sel[j] = 1;
                } else if (ctrl) {
                    w->sel[i] = w->sel[i] ? 0 : 1;
                    w->anchor = i;
                } else {
                    for (size_t j = 0; j < w->sel.size(); j++) w->sel[j] = 0;
                    w->sel[i] = 1;
                    w->anchor = i;
                }
                InvalidateRect(h, NULL, FALSE);
            }
            return 0;
        }
        return 0;
    }

    case WM_LBUTTONUP:
        if (w->dragSb) { w->dragSb = false; ReleaseCapture(); return 0; }
        return 0;

    case WM_LBUTTONDBLCLK: {
        int x = GET_X_LPARAM(lp), y = GET_Y_LPARAM(lp);
        RECT cr; GetClientRect(h, &cr);
        if (x > FM_LEFTW && y >= (FmPcView(w) ? (FM_TITLE_H + FM_TOOL_H) : FmListTop())
            && y < FmStatusTop(h)) {
            int i = FmPcView(w) ? FmTileAt(h, w, x, y)
                                : w->top + (y - FmListTop()) / FM_ROW_H;
            FmOpenItem(w, i);
        }
        return 0;
    }

    case WM_RBUTTONUP: {
        int x = GET_X_LPARAM(lp), y = GET_Y_LPARAM(lp);
        RECT cr; GetClientRect(h, &cr);
        if (x > FM_LEFTW && y >= (FmPcView(w) ? (FM_TITLE_H + FM_TOOL_H) : FmListTop())
            && y < FmStatusTop(h)) {
            int i = FmPcView(w) ? FmTileAt(h, w, x, y)
                                : w->top + (y - FmListTop()) / FM_ROW_H;
            if (i >= 0 && i < (int)w->items.size() && !w->sel[i]) {
                for (size_t j = 0; j < w->sel.size(); j++) w->sel[j] = 0;
                w->sel[i] = 1;
                w->anchor = i;
                InvalidateRect(h, NULL, FALSE);
            }
        }
        FmContextMenu(w, x, y);
        return 0;
    }

    case WM_MOUSEWHEEL: {
        int delta = GET_WHEEL_DELTA_WPARAM(wp);
        int n, vis;
        FmScrollDims(w, n, vis);
        int maxTop = n - vis;
        if (maxTop <= 0) return 0;
        w->top -= (delta / WHEEL_DELTA) * 3;
        if (w->top < 0) w->top = 0;
        if (w->top > maxTop) w->top = maxTop;
        InvalidateRect(h, NULL, FALSE);
        return 0;
    }

    case WM_APP + 30: {                              /* 任务栏/桌面请求导航 */
        std::wstring* p = (std::wstring*)lp;
        if (p) { FmNavigate(w, *p, true); delete p; }
        return 0;
    }

    case WM_CLOSE:
        DestroyWindow(h);
        return 0;

    case WM_DESTROY: {
        SetWindowLongPtrW(h, GWLP_USERDATA, 0);
        for (size_t i = 0; i < g_fms.size(); i++)
            if (g_fms[i] == w) { g_fms.erase(g_fms.begin() + i); break; }
        delete w;
        return 0;
    }
    }
    return DefWindowProcW(h, m, wp, lp);
}

/* ============================ 简易自绘输入框 ============================ */
#define IB_W  S(430)
#define IB_H  S(168)
#define IB_TITLE_H S(28)

static HWND         g_ibWnd = NULL;
static HWND         g_ibEdit = NULL;
static std::wstring* g_ibVal = NULL;
static int          g_ibRet = 1;
static bool         g_ibClass = false;
static RECT         g_ibOk, g_ibCancel;
static std::wstring g_ibTitle, g_ibPrompt;
static int          g_ibHot = -1;          /* 0=确定 1=取消 -1=无 */
static bool         g_ibTracking = false;

static LRESULT CALLBACK IbProc(HWND h, UINT m, WPARAM wp, LPARAM lp)
{
    switch (m) {
    case WM_ERASEBKGND: return 1;

    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC dc = BeginPaint(h, &ps);
        RECT cr; GetClientRect(h, &cr);
        int W = cr.right, H = cr.bottom;

        HDC mem = CreateCompatibleDC(dc);
        HBITMAP dib = CreateCompatibleBitmap(dc, W, H);
        HGDIOBJ old = SelectObject(mem, dib);
        SetBkMode(mem, TRANSPARENT);

        /* 整体白底（同 iced 版的白卡片） */
        HBRUSH wb = CreateSolidBrush(T_CARD);
        FillRect(mem, &cr, wb); DeleteObject(wb);

        /* 标题条：浅灰底 + 下边线 */
        RECT tr = { 0, 0, W, IB_TITLE_H };
        {
            HBRUSH b = CreateSolidBrush(T_SIDE);
            FillRect(mem, &tr, b); DeleteObject(b);
            HLine(mem, 0, W, IB_TITLE_H - 1, T_LINE);
        }
        RECT tt = { S(14), 0, W - S(14), IB_TITLE_H };
        DrawTextR(mem, tt, g_ibTitle.c_str(), g_fontBold, T_TEXT);

        RECT pr = { S(16), S(40), W - S(16), S(62) };
        DrawTextR(mem, pr, g_ibPrompt.c_str(), g_fontUI, T_SUB);

        /* 编辑框外框（EDIT 白底由 WM_CTLCOLOREDIT 提供） */
        {
            RECT eb = { S(16), S(66), W - S(16), S(98) };
            FillRound(mem, eb, FM_RADIUS, T_CARD, T_LINE_STRONG);
        }

        /* 按钮：确定 = 蓝底白字（primary），取消 = 白底描边（ghost） */
        const wchar_t* lbl[2] = { L"\u786E\u5B9A", L"\u53D6\u6D88" };
        for (int i = 0; i < 2; i++) {
            RECT b = (i == 0) ? g_ibOk : g_ibCancel;
            bool hot = (g_ibHot == i);
            COLORREF bg, bd, fg;
            if (i == 0) {
                bg = hot ? RGB(0x32, 0x80, 0xED) : T_ACCENT;
                bd = CLR_INVALID;
                fg = T_CARD;
            } else {
                bg = hot ? T_HOVER : T_CARD;
                bd = hot ? T_LINE_STRONG : T_LINE;
                fg = hot ? T_TEXT : T_SUB;
            }
            FillRound(mem, b, FM_RADIUS, bg, bd);
            DrawTextR(mem, b, lbl[i], g_fontUI, fg,
                      DT_CENTER | DT_SINGLELINE | DT_VCENTER);
        }

        HPEN p = CreatePen(PS_SOLID, 1, T_LINE_STRONG);
        HPEN op = (HPEN)SelectObject(mem, p);
        HBRUSH nb = (HBRUSH)SelectObject(mem, GetStockObject(NULL_BRUSH));
        Rectangle(mem, 0, 0, W, H);
        SelectObject(mem, nb); SelectObject(mem, op); DeleteObject(p);

        BitBlt(dc, 0, 0, W, H, mem, 0, 0, SRCCOPY);
        SelectObject(mem, old); DeleteObject(dib); DeleteDC(mem);
        EndPaint(h, &ps);
        return 0;
    }

    case WM_CTLCOLOREDIT: {
        HDC dc = (HDC)wp;
        SetTextColor(dc, T_TEXT);
        SetBkColor(dc, T_CARD);
        static HBRUSH br = NULL;
        if (!br) br = CreateSolidBrush(T_CARD);
        return (LRESULT)br;
    }

    case WM_MOUSEMOVE: {
        if (!g_ibTracking) {
            TRACKMOUSEEVENT tme = { sizeof(tme), TME_LEAVE, h, 0 };
            TrackMouseEvent(&tme);
            g_ibTracking = true;
        }
        int x = GET_X_LPARAM(lp), y = GET_Y_LPARAM(lp);
        POINT p = { x, y };
        int hot = -1;
        if (PtInRect(&g_ibOk, p)) hot = 0;
        else if (PtInRect(&g_ibCancel, p)) hot = 1;
        if (hot != g_ibHot) { g_ibHot = hot; InvalidateRect(h, NULL, FALSE); }
        return 0;
    }

    case WM_MOUSELEAVE:
        g_ibTracking = false;
        if (g_ibHot != -1) { g_ibHot = -1; InvalidateRect(h, NULL, FALSE); }
        return 0;

    case WM_LBUTTONDOWN: {
        int x = GET_X_LPARAM(lp), y = GET_Y_LPARAM(lp);
        if (y < IB_TITLE_H) { SendMessageW(h, WM_NCLBUTTONDOWN, HTCAPTION, 0); return 0; }
        if (PtInRect(&g_ibOk, POINT{ x, y })) {
            wchar_t buf[MAX_PATH] = { 0 };
            GetWindowTextW(g_ibEdit, buf, MAX_PATH);
            if (g_ibVal) *g_ibVal = buf;
            g_ibRet = 0;
            PostMessageW(h, WM_CLOSE, 0, 0);
            return 0;
        }
        if (PtInRect(&g_ibCancel, POINT{ x, y })) { g_ibRet = 1; PostMessageW(h, WM_CLOSE, 0, 0); return 0; }
        return 0;
    }

    case WM_COMMAND:
        /* 2001 = 编辑框回车；IDOK = IsDialogMessage 对 Enter 的转发 */
        if (HIWORD(wp) == 0 && (LOWORD(wp) == 2001 || LOWORD(wp) == IDOK)) {
            wchar_t buf[MAX_PATH] = { 0 };
            GetWindowTextW(g_ibEdit, buf, MAX_PATH);
            if (g_ibVal) *g_ibVal = buf;
            g_ibRet = 0;
            PostMessageW(h, WM_CLOSE, 0, 0);
        } else if (HIWORD(wp) == 0 && LOWORD(wp) == IDCANCEL) {
            g_ibRet = 1;
            PostMessageW(h, WM_CLOSE, 0, 0);
        }
        return 0;

    case WM_KEYDOWN:
        if (wp == VK_ESCAPE) { g_ibRet = 1; PostMessageW(h, WM_CLOSE, 0, 0); }
        else if (wp == VK_RETURN) {
            wchar_t buf[MAX_PATH] = { 0 };
            GetWindowTextW(g_ibEdit, buf, MAX_PATH);
            if (g_ibVal) *g_ibVal = buf;
            g_ibRet = 0;
            PostMessageW(h, WM_CLOSE, 0, 0);
        }
        return 0;

    case WM_CLOSE:
        DestroyWindow(h);
        return 0;

    case WM_DESTROY:
        g_ibWnd = NULL; g_ibEdit = NULL;
        return 0;
    }
    return DefWindowProcW(h, m, wp, lp);
}

int ZywInputBox(HWND owner, const wchar_t* title, const wchar_t* prompt, std::wstring& value)
{
    if (!g_ibClass) {
        WNDCLASSEXW wc = { sizeof(wc) };
        wc.lpfnWndProc   = IbProc;
        wc.hInstance     = g_hInst;
        wc.hCursor       = LoadCursor(NULL, IDC_ARROW);
        wc.hbrBackground = NULL;
        wc.lpszClassName = L"ZYWPECore_Input";
        RegisterClassExW(&wc);
        g_ibClass = true;
    }

    RECT wa; SystemParametersInfoW(SPI_GETWORKAREA, 0, &wa, 0);
    int L = wa.left + ((wa.right - wa.left) - IB_W) / 2;
    int T = wa.top + ((wa.bottom - wa.top) - IB_H) / 2;

    g_ibVal = &value;
    g_ibRet = 1;
    g_ibHot = -1;
    g_ibTracking = false;
    g_ibTitle  = title  ? title  : L"\u8BF7\u8F93\u5165";
    g_ibPrompt = prompt ? prompt : L"";

    HWND h = CreateWindowExW(WS_EX_TOPMOST, L"ZYWPECore_Input", title ? title : L"",
                             WS_POPUP | WS_VISIBLE, L, T, IB_W, IB_H,
                             owner, NULL, g_hInst, NULL);
    if (!h) return 1;
    g_ibWnd = h;

    g_ibEdit = CreateWindowExW(0, L"EDIT", value.c_str(),
                               WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL,
                               S(22), S(70), IB_W - S(44), S(24), h, (HMENU)2001, g_hInst, NULL);
    if (g_ibEdit && g_fontUI) SendMessageW(g_ibEdit, WM_SETFONT, (WPARAM)g_fontUI, TRUE);

    g_ibOk     = { IB_W - S(196), IB_H - S(48), IB_W - S(108), IB_H - S(16) };
    g_ibCancel = { IB_W - S(100), IB_H - S(48), IB_W - S(16),  IB_H - S(16) };

    if (owner) EnableWindow(owner, FALSE);
    ShowWindow(h, SW_SHOW);
    SetForegroundWindow(h);
    if (g_ibEdit) { SetFocus(g_ibEdit); SendMessageW(g_ibEdit, EM_SETSEL, 0, -1); }

    MSG msg;
    while (g_ibWnd && GetMessageW(&msg, NULL, 0, 0) > 0) {
        /* 回车 / ESC 会被 IsDialogMessageW 吃掉，先自行处理 */
        if (msg.message == WM_KEYDOWN && (msg.wParam == VK_RETURN || msg.wParam == VK_ESCAPE)) {
            SendMessageW(h, WM_KEYDOWN, msg.wParam, 0);
            continue;
        }
        if (!IsDialogMessageW(h, &msg)) {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
    }
    if (owner) { EnableWindow(owner, TRUE); SetForegroundWindow(owner); }
    g_ibVal = NULL;
    return g_ibRet;
}

/* ============================ 对外接口 ============================ */
static void FmEnsureClass()
{
    if (g_fmClassReg) return;
    WNDCLASSEXW wc = { sizeof(wc) };
    wc.lpfnWndProc   = Fm_WndProc;
    wc.hInstance     = g_hInst;
    /* 本程序不带图标资源：窗口类不设 hIcon / hIconSm，
     * 窗口样式也没有 WS_EX_APPWINDOW，任务栏本来就不显示它。 */
    wc.hIcon         = NULL;
    wc.hIconSm       = NULL;
    wc.hCursor       = LoadCursor(NULL, IDC_ARROW);
    wc.hbrBackground = NULL;
    wc.lpszClassName = L"ZYWPECore_Files";
    wc.style         = CS_DBLCLKS;
    RegisterClassExW(&wc);
    g_fmClassReg = true;
}

bool Fm_Open(const wchar_t* path)
{
    FmEnsureClass();
    std::wstring target = path ? TrimStr(path) : L"";
    for (size_t i = 0; i < target.size(); i++) if (target[i] == L'/') target[i] = L'\\';
    while (target.size() > 3 && target.back() == L'\\') target.pop_back();

    /* 已有窗口 → 复用并导航 */
    if (!g_fms.empty()) {
        FmWin* w = g_fms.back();
        if (IsWindow(w->hwnd)) {
            if (!target.empty() || !w->path.empty()) FmNavigate(w, target, true);
            if (IsIconic(w->hwnd)) ShowWindow(w->hwnd, SW_RESTORE);
            SetForegroundWindow(w->hwnd);
            return true;
        }
    }

    /* 默认尺寸 1080 × 720 逻辑像素（与 iced 版「竹影清风PE工具合集」同值）。
     * ★ 必须乘缩放：本程序是系统 DPI 感知的，写死物理像素在 150% 屏上只相当于
     *   逻辑 720×480，看起来又小又挤。
     * ★ 9.2：g_scale 里已经乘了密度系数 UI_DENSITY（0.90），所以这里逻辑尺寸
     *   不变，实际窗口也会比之前小一圈（150% 屏上 1620×1080 → 1458×972）。 */
    RECT wa; SystemParametersInfoW(SPI_GETWORKAREA, 0, &wa, 0);
    int W = S(1080), H = S(720);
    if (W > wa.right - wa.left) W = wa.right - wa.left;
    if (H > wa.bottom - wa.top) H = wa.bottom - wa.top;
    int L = wa.left + ((wa.right - wa.left) - W) / 2;
    int T = wa.top + ((wa.bottom - wa.top) - H) / 2;

    HWND h = CreateWindowExW(0, L"ZYWPECore_Files", APP_TITLE,
                             WS_POPUP | WS_CLIPCHILDREN | WS_VISIBLE,
                             L, T, W, H, NULL, NULL, g_hInst, NULL);
    if (!h) return false;

    FmWin* w = FmFrom(h);
    if (!w) return false;
    FmLayout(w);
    FmNavigate(w, target, true);
    SetForegroundWindow(h);
    return true;
}

void Fm_CloseAll()
{
    std::vector<HWND> hs;
    for (size_t i = 0; i < g_fms.size(); i++) hs.push_back(g_fms[i]->hwnd);
    for (size_t i = 0; i < hs.size(); i++)
        if (IsWindow(hs[i])) DestroyWindow(hs[i]);
    g_fms.clear();
}

int Fm_Count() { return (int)g_fms.size(); }

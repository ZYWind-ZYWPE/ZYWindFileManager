/* 关联图标回退验证：files.cpp 里 AssocIcon() 用的就是这套调用
 * ------------------------------------------------------------------
 * 1) 对若干扩展名走 SHGetFileInfoW(+SHGFI_USEFILEATTRIBUTES) 拿系统「默认应用
 *    关联」图标，检查句柄非空**且**不是全透明空图（PE 空壳 imageres.dll 的症状）
 * 2) 对真实 exe 走不带 USEFILEATTRIBUTES 的调用，验证能取到文件自身图标
 *
 * 编译：cl /nologo /std:c++17 /utf-8 /EHsc /W4 /MT /DUNICODE /D_UNICODE associco.cpp user32.lib shell32.lib gdi32.lib
 */
#include <windows.h>
#include <shellapi.h>
#include <string>
#include <stdio.h>

static bool IconHasPixels(HICON ic, int sz)
{
    if (!ic) return false;
    HDC dc = CreateCompatibleDC(NULL);
    if (!dc) return false;
    BITMAPINFO bi = { 0 };
    bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bi.bmiHeader.biWidth = sz;
    bi.bmiHeader.biHeight = -sz;
    bi.bmiHeader.biPlanes = 1;
    bi.bmiHeader.biBitCount = 32;
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
    DeleteObject(dib); DeleteDC(dc);
    return any;
}

static void TryExt(const wchar_t* path, const wchar_t* label, bool fromFile, int sz)
{
    SHFILEINFOW sfi = { 0 };
    UINT fl = SHGFI_ICON | (sz <= 24 ? SHGFI_SMALLICON : SHGFI_LARGEICON);
    if (!fromFile) fl |= SHGFI_USEFILEATTRIBUTES;
    BOOL ok = SHGetFileInfoW(path, FILE_ATTRIBUTE_NORMAL, &sfi, sizeof(sfi), fl);
    bool pix = ok ? IconHasPixels(sfi.hIcon, sz) : false;
    printf("  %-14s hIcon=%s  pixels=%s\n", label,
           (ok && sfi.hIcon) ? "yes" : "NO ", pix ? "yes" : "NO ");
    if (ok && sfi.hIcon) DestroyIcon(sfi.hIcon);
}

int wmain()
{
    printf("system small icon = %d px, large = %d px\n",
           GetSystemMetrics(SM_CXSMICON), GetSystemMetrics(SM_CXICON));

    printf("--- by extension (SHGFI_USEFILEATTRIBUTES, 16px) ---\n");
    TryExt(L"a.txt",    L".txt",  false, 16);
    TryExt(L"a.pdf",    L".pdf",  false, 16);
    TryExt(L"a.docx",   L".docx", false, 16);
    TryExt(L"a.xlsx",   L".xlsx", false, 16);
    TryExt(L"a.zip",    L".zip",  false, 16);
    TryExt(L"a.dll",    L".dll",  false, 16);
    TryExt(L"a.zzz",    L".zzz",  false, 16);   /* 不存在的扩展名 → 通用图标 */
    TryExt(L"noext",    L"(none)", false, 16);

    printf("--- from real file (own embedded icon, 16px) ---\n");
    wchar_t sys[512];
    GetSystemDirectoryW(sys, 512);
    std::wstring np = std::wstring(sys) + L"\\notepad.exe";
    std::wstring mm = std::wstring(sys) + L"\\mmc.exe";
    TryExt(np.c_str(), L"notepad.exe", true, 16);
    TryExt(mm.c_str(), L"mmc.exe",     true, 16);

    printf("--- 32px (large) ---\n");
    TryExt(L"a.docx", L".docx", false, 32);
    TryExt(np.c_str(), L"notepad.exe", true, 32);
    return 0;
}

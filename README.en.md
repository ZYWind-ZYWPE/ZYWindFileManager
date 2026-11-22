# ZYWFileManager — ZhuYing QingFeng PE File Manager

[简体中文](README.md) | English

> Made by ZhuYing QingFeng (竹影清风) · A FirTeam product | Version 1.0.0.4 (`APP_VERSTR`)

A **file manager built for WinPE**. WinPE ships without `explorer.exe`; this program is the file-management part of that shell: open folders, copy/cut/paste, rename, delete, check disk space — all done here.

- Language / framework: **C++17 + native Win32 / GDI**, no MFC / Qt / third-party libraries
- Runtime: **statically linked (`/MT`)** — copy and run, no VC redistributable needed
- Artifacts: one `竹影清风PE文件资源管理器.exe` + one `zywico.dll` (icon library, **must sit next to the exe**)
- Origin: the file-manager module extracted from "ZhuYing QingFeng PE Core (ZYWPECore)" into a standalone project (`main.cpp` is a freshly written entry point; `files.cpp` / `util.cpp` / `zywpe.h` are trimmed down to what the file manager needs; `theme.h` is new for this project)

The UI is a **plain white minimalist style**; colors / title bar / sidebar / element styling map one-to-one to the Rust + iced "ZhuYing QingFeng PE Tool Collection" (see `src\theme.h`).

---

## 1. Features (all backed by the source)

### Window
- **Fully custom-drawn, no system title bar**: `WM_NCCALCSIZE` returns 0 (client area = the whole window) and `WM_NCHITTEST` marks the title-bar area as `HTCAPTION` for dragging
- Custom-drawn Minimize / Maximize / Close buttons (close turns red-on-hover); double-click the title bar to toggle maximize
- 6px resizable edges when not maximized; minimum size 720×460 (logical pixels)
- Maximized size follows the work area (`SPI_GETWORKAREA`) so it never covers the taskbar

### Toolbar + address bar
- Toolbar with 5 buttons: **Back / Forward / Up / Refresh / New folder** (pure line drawing, stroke width scales too)
  - Unavailable states gray out automatically (Back grays with no history; Up and New folder gray on "This PC")
- The address bar is an editable `EDIT` control (**subclassed** to receive Enter, `FmEditProc`):
  - Enter on a **directory** → navigate; Enter on a **file** → hand it to the Shell to open (warning only if the path doesn't exist)
  - Paths relative to the current folder work (typing `PE` while in `D:\Tools` → `D:\Tools\PE`); `C:` is auto-completed to `C:\`
  - `此电脑` / `This PC` / `::` returns to the home page; `Esc` cancels editing and restores the address bar
- Navigation history up to 64 entries; Back / Forward use `hist` + `histPos`

### Home page "This PC" = Devices and drives grid
Imitates the Win10 Explorer home: a "Devices and drives" group title over a tile grid, each tile = 48px drive icon + volume label `(X:)` + capacity bar + `xx GB free of xx GB`.

- The capacity bar fills by used ratio and turns red above 90%
- Drive types are auto-detected: hard disk / removable (USB) / optical / network / RAM disk (`GetDriveTypeW`)
- Tile columns adapt to window width; scrolling counts by "rows" (detail view counts by "items" instead)

### File list (details view)
- Four columns: **Name / Size / Type / Modified**, click a header to sort (click again to flip; header shows an arrow)
- Directories **always sort first**; sizes use 3 significant digits (Win10 habit: `34.5 GB`, `256 MB`)
- Type text comes from the Shell description (`SHGetFileInfoW`'s `szTypeName`)
- Multi-select: Ctrl-click, Shift-range; arrow keys move with an anchor; custom-drawn scrollbar (click track to page, drag the thumb, wheel scrolls)

### Operations
- Double-click opens: directories enter, files open via the Shell
- Copy / cut / paste / delete / delete permanently / rename / new folder / properties, all through `SHFileOperationW`
- **Recycle-bin detection before delete**: if the target volume has no `$Recycle.Bin` (common on WinPE, the X: drive, or FAT partitions) it falls back to a permanent delete and the confirmation says plainly "there is no Recycle Bin here — deletion cannot be undone"
- New folder / rename use the custom-drawn `ZywInputBox` (modal, Enter to confirm, ESC to cancel, filters `\ / : * ? " < > |`, auto-numbered duplicates)
- Context menu: Open / Open with Notepad / Copy / Cut / Paste / Rename / Delete / Delete permanently / **New ▸ (Folder / Text Document)** / Refresh / Select all / Properties

### Keyboard shortcuts

| Key | Action |
|---|---|
| `Enter` | Open selection |
| `Backspace` | Go up one level |
| `Delete` / `Shift+Delete` | Delete / delete permanently |
| `F2` / `F5` | Rename / refresh |
| `Ctrl+A` `Ctrl+C` `Ctrl+X` `Ctrl+V` | Select all / copy / cut / paste |
| `↑` `↓` | Move selection (combine with Ctrl / Shift) |

---

## 2. Two "PE-special" key designs

### 1. Built-in icons first; ask the system's "default app association" only when uncovered
In WinPE, `imageres.dll` is often trimmed to 12 KB — an empty shell with **zero icon resources**. `SHGetFileInfoW` still "succeeds" and returns an index, but drawing it yields full transparency (everything loses its icon); mixing system and built-in icons makes some rows show icons while others don't.

So the main path is the **built-in icon library** `zywico.dll`: search order = exe's folder → `%SystemRoot%\System32\zywico.dll` → `%SystemRoot%\System32\imageres.dll` (fallback).

The built-in library covers 15 common file classes; anything else (`.docx` / `.pdf` / `.xlsx` / extensionless…) used to fall onto the same "generic file" icon. Those now **ask the system for the default-app-association icon** (`SHGetFileInfoW` follows the extension to the Word / Edge / 7-Zip icon; `exe / lnk / msi` take the file's own embedded icon). Classes covered by the built-in library stay untouched, keeping a uniform style.

**Pitfall guard**: every system icon is probed once for "is it an empty image" (drawn onto a 32bpp DIB, counting non-transparent pixels); if empty, that path is disabled entirely and the built-in icon is used — so even with a hollow `imageres.dll` in PE, the list never degrades into "some icons, some not". The probe runs once per size; icons are cached by extension (by full path for executables), capped at 256 entries.

`zywico.dll` is compiled from the ico files under `winico\_trim` (only ≤48px frames kept, same-size duplicates removed, 334 KB). **Resource ID = the shell32.dll icon index**; the current 15-class mapping lives in `kKindIconId` in `src\files.cpp`:

| Class | ID | Class | ID |
|---|---|---|---|
| This PC | 16 | Images | 226 |
| Folder | 4 | Archives | 173 |
| Local disk | 9 | Audio | 41 |
| Optical drive | 12 | Video | 224 |
| Removable (USB) | 8 | PowerShell (`ps1`) | 160 |
| Application (`exe`) | 3 | Batch (`bat/cmd`) | 153 |
| Text document | 152 | `dll` / `sys` | 154 |
| Generic file | 1 | | |

### 2. DPI and fonts
- The process is **system-DPI aware**: `SetProcessDPIAware()` → `InitDpi()`; every size is written in 96-DPI "logical pixels" and converted with `S(v)`
- Scale = system DPI × density factor (default `UI_DENSITY 0.90`); at runtime the env var `ZYW_UI_SCALE` (0.5–2.0) overrides it without recompiling
- PE font sets are often trimmed to Latin-only fonts, which turns every Chinese glyph into a box: the app therefore **tests candidate fonts one by one for a real 「中」 glyph** (`GetGlyphIndicesW`) with an existence-based fallback; text is always `CLEARTYPE_QUALITY` to keep small sizes from graying out
- Icon edges in the list / sidebar use the real system small-icon size (`SM_CXSMICON`, 24 at 150% rather than 16); hardcoding 16 makes icons sit low in the row and overflow

---

## 3. Directory layout

```
ZYWFileManager.sln / .vcxproj / .filters   VS2026 project (x64 / Win32, Debug / Release)
build.bat                                  command-line build script
src\
  main.cpp      Entry: WinMain, command line, logging
  files.cpp     Body: window / drawing / navigation / list / menus / icons / custom input box
  util.cpp      DPI, font selection, path & process helpers
  zywpe.h       Shared declarations (trimmed from ZYWPECore)
  theme.h       Color & metric constants (mirrors the iced "ZhuYing QingFeng PE Tool Collection")
res\
  app.rc        Version-info resource
  app.manifest  DPI awareness / common-controls manifest
winico\
  _trim\        Icon library sources: trimmed icos + winico.rc + mkdll.bat
  图标对照.html  Preview page of the 15 icon classes
```

---

## 4. Building

Environment: Visual Studio 2026 (MSVC 14.51 toolset) + Windows SDK 10.0.28000 (paths live in `VSDIR / MSVC / SDK / SDKVER` at the top of `build.bat` — adjust those four lines if installed elsewhere).

```bat
build.bat              :: Release x64 (default)
build.bat x86          :: Release x86 (32-bit PE)
build.bat debug        :: Debug x64
build.bat x86 debug    :: Debug x86
```

Output lands in `bin\<platform>\<config>\竹影清风PE文件资源管理器.exe`.

Equivalent command without the script:

```bat
rc /nologo /fo build\obj\x64\Release\app.res /i res res\app.rc
cl /nologo /std:c++17 /utf-8 /EHsc /W3 /MT /O2 /GF /Gy /DNDEBUG ^
   /DUNICODE /D_UNICODE /D_WIN32_WINNT=0x0600 ^
   src\main.cpp src\files.cpp src\util.cpp ^
   /Fo"build\obj\x64\Release\\" build\obj\x64\Release\app.res ^
   /Fe:"bin\x64\Release\竹影清风PE文件资源管理器.exe" ^
   user32.lib gdi32.lib comctl32.lib shell32.lib shlwapi.lib ole32.lib ^
   advapi32.lib oleaut32.lib comdlg32.lib windowscodecs.lib uuid.lib ^
   /link /SUBSYSTEM:WINDOWS /MACHINE:X64 /OPT:REF /OPT:ICF
```

**Rebuilding the icon library** (after changing the icon mapping table):

```bat
cd winico\_trim
rc   /nologo /fo winico.res winico.rc
link /nologo /DLL /NOENTRY /MACHINE:X64 /OUT:..\..\zywico.dll winico.res
```

(These are exactly the contents of `winico\_trim\mkdll.bat`; it also copies the result to `bin\x64\Release\`.)

---

## 5. Running

```bat
竹影清风PE文件资源管理器.exe                  :: open "This PC"
竹影清风PE文件资源管理器.exe "D:\"             :: open drive D:
竹影清风PE文件资源管理器.exe "D:\Tools"        :: open that folder
竹影清风PE文件资源管理器.exe "D:\Tools\a.txt"  :: open the file's folder (falls back automatically)
```

Log file: `%TEMP%\ZYWFileManager.log` (UTF-8 with BOM, appended). It records the version, DPI, density, effective scale, system small-icon size and the chosen font at startup — check those lines first when debugging "blurry text / boxes instead of Chinese / UI too big or small" in PE.

**Deployment**: `zywico.dll` must sit in the same folder as the exe, or every file icon will be blank.

---

## 6. Known issues → fixed (2026-10-24)

A full `/W4 /analyze` static check once listed 5 confirmed bugs; **all are fixed now**:

| # | Issue | Fix |
|---|---|---|
| 1 | Address-bar Enter did nothing | `WM_COMMAND`'s `HIWORD(wp)==0` was a dead branch (a single-line EDIT never sends it). The address bar is now **subclassed** (`FmEditProc`), which catches `WM_KEYDOWN + VK_RETURN` and calls `FmGo` |
| 2 | Typing a folder path reported "not found" | `FmGo` now uses `GetFileAttributesW` directly: directory → navigate, file → Shell-open; also supports paths relative to the current folder and auto-appends `\` to `C:` |
| 3 | Home-page scrollbar drag was dead | The `WM_MOUSEMOVE` drag branch now uses `FmScrollDims` + `sbTop`, matching `WM_LBUTTONDOWN` |
| 4 | Blank pane after shrinking the window | `WM_SIZE` clamps `w->top` against the new `n/vis` |
| 5 | Drives could be deleted / copied / cut on the home page | `FmCopyToClip` / `FmDelete` / `FmRename` / `FmRunOnSel` all filter by `isDrive`; the context menu grays Copy / Cut / Delete / Rename for drives, keeping Open / Properties |

The remainder are warning-only findings with no functional impact (unused parameters, variable shadowing, dead `FmToolY`, BOM constant casts).

---

## 7. Credits

Made by ZhuYing QingFeng (竹影清风) · A FirTeam product (always shown at the bottom-left of the status bar).

The `zywico.dll` icon library is compiled from "Sun Valley style" shell32 icons extracted and re-created by a third party, used here for UI display only; all rights belong to their original authors.

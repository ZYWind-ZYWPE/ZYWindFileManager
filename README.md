# 竹影清风PE文件资源管理器（ZYWFileManager）

简体中文 | [English](README.en.md)

> 竹影清风制作 · FirTeam 出品　|　版本 1.0.0.4（`APP_VERSTR`）

一个**给 WinPE 用的文件资源管理器**。WinPE 里没有 `explorer.exe`，这个程序就是那套 Shell 的文件管理部分：打开目录、复制剪切粘贴、重命名、删除、看容量，都在这里完成。

- 语言 / 框架：**C++17 + 原生 Win32 / GDI**，不依赖 MFC / Qt / 任何第三方库
- 运行时：**静态链接（`/MT`）**，拷过去就能跑，不用装 VC 运行库
- 产物：一个 `竹影清风PE文件资源管理器.exe` + 一个 `zywico.dll`（图标库，**必须与 exe 同目录**）
- 来源：从「竹影清风PE核心（ZYWPECore）」里把文件管理器模块整块剥离出来独立成工程（`main.cpp` 是独立新写的入口，`files.cpp` / `util.cpp` / `zywpe.h` 均已按「只留文件管理器用得到的代码」裁剪过；`theme.h` 为本工程新增）

界面为**纯白简约风格**，配色 / 标题栏 / 侧栏 / 元素样式与 Rust + iced 版的「竹影清风PE工具合集」一一对应（见 `src\theme.h`）。

---

## 一、功能（均对应源码实现）

### 窗口
- **完全自绘、无系统标题栏**：`WM_NCCALCSIZE` 返回 0（客户区＝整个窗口），`WM_NCHITTEST` 把标题栏区域判为 `HTCAPTION` 实现拖动
- 自绘的 最小化 / 最大化 / 关闭 三个按钮（关闭键悬停转红底白字）；双击标题栏切换最大化
- 非最大化时 6px 边缘可拉伸；最小尺寸 720×460（逻辑像素）
- 最大化尺寸按工作区计算（`SPI_GETWORKAREA`），不会盖住任务栏

### 工具栏 + 地址栏
- 工具栏 5 个按钮：**后退 / 前进 / 上级 / 刷新 / 新建文件夹**（纯线条绘制，线宽随缩放一起变）
  - 不可用状态自动变灰（无历史时后退灰、「此电脑」下上级与新建文件夹灰）
- 地址栏是可编辑的 `EDIT`（**已子类化**接收回车，`FmEditProc`）：
  - 输入**目录**回车 → 跳转；输入**文件**回车 → 交给 Shell 打开（路径不存在才弹提示）
  - 支持相对当前目录的路径（当前在 `D:\Tools` 时输入 `PE` → `D:\Tools\PE`）、`C:` 自动补成 `C:\`
  - `此电脑` / `This PC` / `::` 回首页；`Esc` 放弃编辑并还原地址栏
- 导航历史最多 64 条，前进 / 后退靠 `hist` + `histPos`

### 首页「此电脑」＝ 设备和驱动器网格
仿 Win10 资源管理器首页：组标题「设备和驱动器」+ 磁贴网格，每个磁贴 = 48px 驱动器图标 + 卷标 `(X:)` + 容量条 + `xx GB 可用，共 xx GB`。

- 容量条按已用比例填充，用量 > 90% 转红
- 驱动器类型自动区分：硬盘 / 可移动存储（U 盘）/ 光盘 / 网络驱动器 / RAM 盘（`GetDriveTypeW`）
- 磁贴网格按窗口宽度自动算列数；滚动按「行」计（和详情视图按「条目」计不同）

### 文件列表（详情视图）
- 四列：**名称 / 大小 / 类型 / 修改日期**，点列头排序（再点一次切升序降序，列头带箭头）
- 目录**恒排在最前**；容量用 3 位有效数字显示（对齐 Win10 习惯：`34.5 GB`、`256 MB`）
- 类型文字取 Shell 描述（`SHGetFileInfoW` 的 `szTypeName`）
- 多选：Ctrl 点选、Shift 连选；方向键上下移动并用锚点连选；自绘滚动条（点空白翻页、拖动滑块、滚轮滚动）

### 操作
- 双击打开：目录进入，文件按 Shell 方式打开
- 复制 / 剪切 / 粘贴 / 删除 / 彻底删除 / 重命名 / 新建文件夹 / 属性，全部走 `SHFileOperationW`
- **删除前判断回收站**：目标卷上没有 `$Recycle.Bin`（WinPE、X: 盘、FAT 分区上很常见）就按永久删除处理，并在确认框里明说「此位置没有回收站，删除后不可恢复」，不让用户误以为能还原
- 新建文件夹 / 重命名用自绘输入框 `ZywInputBox`（模态、回车确认、ESC 取消、自动过滤 `\ / : * ? " < > |`、重名自动加序号）
- 右键菜单：打开 / 用记事本打开 / 复制 / 剪切 / 粘贴 / 重命名 / 删除 / 彻底删除 / **新建 ▸（新建文件夹 / 新建文本文档）** / 刷新 / 全选 / 属性

### 快捷键

| 键 | 作用 |
|---|---|
| `Enter` | 打开选中项 |
| `Backspace` | 返回上一级 |
| `Delete` / `Shift+Delete` | 删除 / 彻底删除 |
| `F2` / `F5` | 重命名 / 刷新 |
| `Ctrl+A` `Ctrl+C` `Ctrl+X` `Ctrl+V` | 全选 / 复制 / 剪切 / 粘贴 |
| `↑` `↓` | 移动选中（配合 Ctrl / Shift 连选） |

---

## 二、两个「PE 特供」的关键设计

### 1. 图标自带优先，取不到才问系统的「默认应用关联」
PE 里的 `imageres.dll` 常被裁到只剩 12 KB —— 空壳、**一个图标资源都没有**。此时 `SHGetFileInfoW` 照样「成功」返回索引，但画出来是全透明的（表现：所有文件都没图标）；而「有的用系统、有的用自带」混用会出现同一屏里有的有图标、有的没有。

所以主路径是**程序自带的图标库** `zywico.dll`：查找顺序 = 程序同目录 → `%SystemRoot%\System32\zywico.dll` → `%SystemRoot%\System32\imageres.dll`（兜底）。

自带库只覆盖 15 类常见文件，剩下的（`.docx` / `.pdf` / `.xlsx` / 无扩展名…）本来都落在同一张「通用文件」图标上。这些类型改为**问系统要「默认应用关联」的图标**（`SHGetFileInfoW`，顺着扩展名找到 Word、Edge、7-Zip…的图标；`exe / lnk / msi` 直接取文件自身内嵌的那张），自带库覆盖到的类型保持原样不动，风格统一。

**防坑**：每次取到系统图标都要先做一次「是不是空图」探测（画到 32bpp DIB 上数非透明像素），空就整条禁用、回退自带图标 —— 所以 PE 里即使 `imageres.dll` 是空壳，也不会退化成「有的有图标有的没有」。首次探测每个尺寸只做一次；图标按「扩展名」（可执行文件按完整路径）缓存，上限 256 条。

`zywico.dll` 由 `winico\_trim` 下的 ico 编译而成（只保留 ≤48px 帧、同尺寸帧去重，334 KB），**资源 ID = shell32.dll 的图标索引**，当前 15 类映射在 `src\files.cpp` 的 `kKindIconId`：

| 类别 | ID | 类别 | ID |
|---|---|---|---|
| 此电脑 | 16 | 图片 | 226 |
| 文件夹 | 4 | 压缩包 | 173 |
| 本地磁盘（硬盘） | 9 | 音频 | 41 |
| 光盘驱动器 | 12 | 视频 | 224 |
| 可移动存储（U 盘） | 8 | PowerShell（`ps1`） | 160 |
| 应用程序（`exe`） | 3 | 批处理（`bat/cmd`） | 153 |
| 文本文档 | 152 | `dll` / `sys` | 154 |
| 通用文件 | 1 | | |

### 2. DPI 与字体
- 程序是**系统 DPI 感知**的：`SetProcessDPIAware()` → `InitDpi()`，所有尺寸按 96 DPI 的「逻辑像素」书写，用 `S(v)` 换算成物理像素
- 缩放 = 系统 DPI 缩放 × 密度系数（默认 `UI_DENSITY 0.90`），运行时可用环境变量 `ZYW_UI_SCALE`（0.5~2.0）覆盖，不必重编译
- PE 的字体集经常被裁到只剩英文字体，界面会「中文全变方框」：因此**逐个候选字体实测能否画出「中」字**（`GetGlyphIndicesW`），选不出就用「存在即可」兜底；字号一律 `CLEARTYPE_QUALITY`，避免小字发灰
- 列表 / 侧栏里的图标边长取系统真实小图标尺寸（`SM_CXSMICON`，150% 时是 24 而不是 16），写死 16 会让图标在行里偏下并溢出

---

## 三、目录结构

```
ZYWFileManager.sln / .vcxproj / .filters   VS2026 工程（x64 / Win32，Debug / Release）
build.bat                                  命令行编译脚本
src\
  main.cpp      入口：WinMain、命令行参数、日志实现
  files.cpp     本体：窗口 / 绘制 / 导航 / 列表 / 菜单 / 图标 / 自绘输入框
  util.cpp      DPI、字体挑选、路径与进程工具
  zywpe.h       公共声明（从 ZYWPECore 裁剪）
  theme.h       配色与尺寸常量（与 iced 版「竹影清风PE工具合集」一一对应）
res\
  app.rc        版本信息资源
  app.manifest  DPI 感知 / 通用控件清单
winico\
  _trim\        图标库原料：精简后的 ico + winico.rc + mkdll.bat
  图标对照.html 15 类图标的对照预览页
```

---

## 四、编译

环境：Visual Studio 2026（MSVC 14.51 工具集）+ Windows SDK 10.0.28000（路径写在 `build.bat` 顶部的 `VSDIR / MSVC / SDK / SDKVER`，装的位置不一样改这四行）。

```bat
build.bat              :: Release x64（默认）
build.bat x86          :: Release x86（32 位 PE）
build.bat debug        :: Debug x64
build.bat x86 debug    :: Debug x86
```

产物在 `bin\<平台>\<配置>\竹影清风PE文件资源管理器.exe`。

不用脚本时等价的命令：

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

**重编图标库**（改了图标映射表之后）：

```bat
cd winico\_trim
rc   /nologo /fo winico.res winico.rc
link /nologo /DLL /NOENTRY /MACHINE:X64 /OUT:..\..\zywico.dll winico.res
```

（这两条就是 `winico\_trim\mkdll.bat` 的内容，编完会顺手拷一份到 `bin\x64\Release\`。）

---

## 五、运行

```bat
竹影清风PE文件资源管理器.exe                  :: 打开「此电脑」
竹影清风PE文件资源管理器.exe "D:\"             :: 打开 D 盘
竹影清风PE文件资源管理器.exe "D:\Tools"        :: 打开该目录
竹影清风PE文件资源管理器.exe "D:\Tools\a.txt"  :: 打开该文件所在目录（自动退到所在目录）
```

日志文件：`%TEMP%\ZYWFileManager.log`（UTF-8 带 BOM，追加写入）。开头会记录版本号、DPI、密度、实际缩放、系统小图标尺寸和实际选中的字体 —— PE 里排查「字发虚 / 中文变方框 / 界面太大太小」先看这几行。

**部署**：`zywico.dll` 必须和 exe 放在同一个目录，否则所有文件图标都是空的。

---

## 六、已知问题 → 已修复（2026-10-24）

`/W4 /analyze` 全量静态检查曾列出 5 条确认存在的问题，**现已全部修复**：

| # | 问题 | 修法 |
|---|---|---|
| 1 | 地址栏回车无效 | `WM_COMMAND` 的 `HIWORD(wp)==0` 是死分支（单行 EDIT 从不发它）。改为**子类化地址栏**（`FmEditProc`），在子类里拦 `WM_KEYDOWN + VK_RETURN` 调 `FmGo` |
| 2 | 输入目录路径被判"不存在" | `FmGo` 改用 `GetFileAttributesW` 自判：目录→导航，文件→交给 Shell 打开；并支持相对当前目录的路径、`C:` 自动补 `\` |
| 3 | 首页滚动条拖不动 | `WM_MOUSEMOVE` 拖拽分支改走 `FmScrollDims` + `sbTop`，与 `WM_LBUTTONDOWN` 一致 |
| 4 | `WM_SIZE` 后 `top` 越界导致空白 | `WM_SIZE` 里按新的 `n/vis` 夹紧 `w->top` |
| 5 | 首页可对驱动器删除 / 复制 / 剪切 | `FmCopyToClip` / `FmDelete` / `FmRename` / `FmRunOnSel` 全部按 `isDrive` 过滤；右键菜单对驱动器把 复制/剪切/删除/重命名 置灰，只留 打开 / 属性 |

余下的都是无功能影响的编译告警（未用参数、变量遮蔽、死函数 `FmToolY`、BOM 常量强转）。

---

## 七、署名

竹影清风制作 · FirTeam 出品（程序状态栏左下角固定显示）。

图标库 `zywico.dll` 由第三方提取并二次创作的「太阳谷风格」shell32 图标精简编译而成，仅用于本项目界面显示，版权归原作者所有。

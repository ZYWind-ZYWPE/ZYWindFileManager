@echo off
chcp 936 >nul 2>&1
setlocal enabledelayedexpansion
title 竹影清风PE文件资源管理器 - 编译脚本

rem ============================================================
rem  竹影清风PE文件资源管理器 (ZYWFileManager) 命令行编译脚本
rem  竹影清风制作  FirTeam出品
rem
rem  用法：
rem    build.bat                 编译 Release x64（默认）
rem    build.bat x86             编译 Release x86（32 位 PE）
rem    build.bat debug           编译 Debug x64
rem    build.bat x86 debug       编译 Debug x86
rem
rem  产物：bin\<平台>\<配置>\竹影清风PE文件资源管理器.exe
rem
rem  说明：日常开发建议直接用 Visual Studio 打开 ZYWFileManager.sln；
rem        本脚本用于不开 VS 时的快速编译 / 批量出包。
rem ============================================================

set "ARCH=x64"
set "CFG=Release"

:args
if "%~1"=="" goto begin
if /i "%~1"=="x64"   set "ARCH=x64"
if /i "%~1"=="x86"   set "ARCH=x86"
if /i "%~1"=="debug"   set "CFG=Debug"
if /i "%~1"=="release" set "CFG=Release"
shift
goto args

:begin
pushd "%~dp0"

set "VSDIR=C:\Program Files\Microsoft Visual Studio\18\Community"
set "MSVC=%VSDIR%\VC\Tools\MSVC\14.51.36231"
set "SDK=C:\Program Files (x86)\Windows Kits\10"
set "SDKVER=10.0.28000.0"

echo.
echo ============================================================
echo   竹影清风PE文件资源管理器  -  编译
echo   竹影清风制作  FirTeam出品
echo ============================================================
echo.

if not exist "%MSVC%\bin" (
  echo [!] 找不到 MSVC 工具集: %MSVC%
  echo     请确认已安装 Visual Studio 2026（工作负载：使用 C++ 的桌面开发）
  echo     若装在别处，请修改本脚本顶部的 VSDIR / MSVC 两行。
  popd ^& exit /b 1
)
if not exist "%SDK%\Include\%SDKVER%" (
  echo [!] 找不到 Windows SDK %SDKVER%
  echo     请安装对应 SDK，或把本脚本顶部的 SDKVER 改成已安装的版本。
  popd ^& exit /b 1
)

rem ---------- 按平台/配置拼环境 ----------
if /i "%ARCH%"=="x86" (
  set "BINDIR=%MSVC%\bin\Hostx64\x86"
  set "MSVCLIB=%MSVC%\lib\x86"
  set "SDKLIB=%SDK%\Lib\%SDKVER%\ucrt\x86;%SDK%\Lib\%SDKVER%\um\x86"
  set "SDKBIN=%SDK%\bin\%SDKVER%\x86"
  set "MACH=X86"
  set "PLAT=Win32"
) else (
  set "BINDIR=%MSVC%\bin\Hostx64\x64"
  set "MSVCLIB=%MSVC%\lib\x64"
  set "SDKLIB=%SDK%\Lib\%SDKVER%\ucrt\x64;%SDK%\Lib\%SDKVER%\um\x64"
  set "SDKBIN=%SDK%\bin\%SDKVER%\x64"
  set "MACH=X64"
  set "PLAT=x64"
)

if /i "%CFG%"=="Debug" (
  set "RTFLAG=/MTd /Od /Zi"
  set "EXTRA=/D_DEBUG"
) else (
  set "RTFLAG=/MT /O2 /GF /Gy"
  set "EXTRA=/DNDEBUG"
)

set "OBJDIR=build\obj\%PLAT%\%CFG%"
set "OUTDIR=bin\%PLAT%\%CFG%"
set "EXENAME=竹影清风PE文件资源管理器.exe"

set "PATH=%BINDIR%;%SDKBIN%;%PATH%"
set "INCLUDE=%MSVC%\include;%SDK%\Include\%SDKVER%\ucrt;%SDK%\Include\%SDKVER%\um;%SDK%\Include\%SDKVER%\shared"
set "LIB=%MSVCLIB%;%SDKLIB%"

echo [i] 平台配置 : %PLAT% / %CFG%
echo [i] 工具集   : %MSVC%
echo [i] SDK      : %SDKVER%
echo.

if not exist "%OBJDIR%" mkdir "%OBJDIR%"
if not exist "%OUTDIR%" mkdir "%OUTDIR%"

rem ---------- 1. 资源（版本信息 / 清单） ----------
echo [*] 编译资源 ...
"%SDKBIN%\rc.exe" /nologo /fo "%OBJDIR%\app.res" ^
    /i "%SDK%\Include\%SDKVER%\um" ^
    /i "%SDK%\Include\%SDKVER%\shared" ^
    /i res res\app.rc
if errorlevel 1 (
  echo [!] 资源编译失败
  popd ^& exit /b 1
)

rem ---------- 2. 编译 + 链接 ----------
echo [*] 编译 ZYWFileManager (%MACH%) ...
cl /nologo /std:c++17 /utf-8 /EHsc /W3 %RTFLAG% %EXTRA% ^
   /DUNICODE /D_UNICODE /D_WIN32_WINNT=0x0600 ^
   src\main.cpp src\files.cpp src\util.cpp ^
   /Fo"%OBJDIR%\\" ^
   "%OBJDIR%\app.res" ^
   /Fe:"%OUTDIR%\%EXENAME%" ^
   user32.lib gdi32.lib comctl32.lib shell32.lib shlwapi.lib ole32.lib ^
   advapi32.lib oleaut32.lib comdlg32.lib windowscodecs.lib uuid.lib ^
   /link /SUBSYSTEM:WINDOWS /MACHINE:%MACH% /OPT:REF /OPT:ICF

set "RC=%ERRORLEVEL%"
echo.
if not "%RC%"=="0" (
  echo [!] 编译失败（退出码 %RC%）
  popd ^& exit /b %RC%
)

for %%A in ("%OUTDIR%\%EXENAME%") do set "FSIZE=%%~zA"
set /a "SIZEK=!FSIZE!/1024"

echo [+] 编译成功
echo     产物 : %OUTDIR%\%EXENAME%
echo     大小 : !SIZEK! KB
echo.
echo ------------------------------------------------------------
echo  运行方式
echo ------------------------------------------------------------
echo   打开此电脑   : "%OUTDIR%\%EXENAME%"
echo   打开指定目录 : "%OUTDIR%\%EXENAME%" "D:\"
echo   打开文件位置 : "%OUTDIR%\%EXENAME%" "D:\Tools\a.txt"
echo   日志文件     : %%TEMP%%\ZYWFileManager.log
echo ------------------------------------------------------------
echo.

popd
endlocal
exit /b 0

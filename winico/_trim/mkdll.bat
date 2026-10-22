@echo off
setlocal
set "MSVC=C:\Program Files\Microsoft Visual Studio\18\Community\VC\Tools\MSVC\14.51.36231"
set "SDK=C:\Program Files (x86)\Windows Kits\10"
set "SDKVER=10.0.28000.0"
set "PATH=%MSVC%\bin\Hostx64\x64;%SDK%\bin\%SDKVER%\x64;%PATH%"

echo [*] rc ...
rc /nologo /fo winico.res winico.rc
if errorlevel 1 (echo [!] rc failed & exit /b 1)

echo [*] link ...
link /nologo /DLL /NOENTRY /MACHINE:X64 /OUT:..\..\zywico.dll winico.res
if errorlevel 1 (echo [!] link failed & exit /b 1)

copy /y ..\..\zywico.dll ..\..\bin\x64\Release\zywico.dll >nul
echo [+] done
for %%A in ("..\..\zywico.dll") do echo     size = %%~zA bytes
exit /b 0

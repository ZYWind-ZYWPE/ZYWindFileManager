@echo off
setlocal
set "VSDIR=C:\Program Files\Microsoft Visual Studio\18\Community"
set "MSVC=%VSDIR%\VC\Tools\MSVC\14.51.36231"
set "SDK=C:\Program Files (x86)\Windows Kits\10"
set "SDKVER=10.0.28000.0"

set "PATH=%MSVC%\bin\Hostx64\x64;%SDK%\bin\%SDKVER%\x64;%PATH%"
set "INCLUDE=%MSVC%\include;%SDK%\Include\%SDKVER%\ucrt;%SDK%\Include\%SDKVER%\um;%SDK%\Include\%SDKVER%\shared"
set "LIB=%MSVC%\lib\x64;%SDK%\Lib\%SDKVER%\ucrt\x64;%SDK%\Lib\%SDKVER%\um\x64"

cd /d "%~dp0"
cl /nologo /std:c++17 /utf-8 /EHsc /W4 /analyze /DUNICODE /D_UNICODE /D_WIN32_WINNT=0x0600 /DNDEBUG /c ..\src\main.cpp ..\src\files.cpp ..\src\util.cpp
echo EXITCODE=%ERRORLEVEL%
endlocal

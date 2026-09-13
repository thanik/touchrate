@echo off
setlocal EnableDelayedExpansion
rem ==========================================================================
rem  TouchRate build script (MSVC)
rem    build.bat            release build  -> build\TouchRate.exe
rem    build.bat debug      debug build
rem    build.bat clean      remove build outputs
rem ==========================================================================

cd /d "%~dp0"

if /i "%~1"=="clean" (
    if exist build rmdir /s /q build
    echo [TouchRate] cleaned.
    exit /b 0
)

set "CONFIG=release"
if /i "%~1"=="debug" set "CONFIG=debug"

rem -------------------------------------------------- locate the MSVC toolchain
if defined VCINSTALLDIR goto :have_env

set "VCVARS="
set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
if not exist "!VSWHERE!" set "VSWHERE=%ProgramFiles%\Microsoft Visual Studio\Installer\vswhere.exe"

if exist "!VSWHERE!" (
    for /f "usebackq delims=" %%i in (`"!VSWHERE!" -latest -prerelease -products * ^
        -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do (
        if exist "%%i\VC\Auxiliary\Build\vcvars64.bat" set "VCVARS=%%i\VC\Auxiliary\Build\vcvars64.bat"
    )
)

if not defined VCVARS (
    for %%r in ("%ProgramFiles%\Microsoft Visual Studio" "%ProgramFiles(x86)%\Microsoft Visual Studio") do (
        for %%v in (18 2022 2019) do (
            for %%e in (Enterprise Professional Community BuildTools Preview) do (
                if not defined VCVARS if exist "%%~r\%%v\%%e\VC\Auxiliary\Build\vcvars64.bat" (
                    set "VCVARS=%%~r\%%v\%%e\VC\Auxiliary\Build\vcvars64.bat"
                )
            )
        )
    )
)

if not defined VCVARS (
    echo [TouchRate] ERROR: could not find vcvars64.bat.
    echo             Install "Desktop development with C++" or run this from a
    echo             "x64 Native Tools Command Prompt".
    exit /b 1
)

echo [TouchRate] using !VCVARS!
call "!VCVARS!" >nul
if errorlevel 1 (
    echo [TouchRate] ERROR: failed to initialise the MSVC environment.
    exit /b 1
)

:have_env
if not exist build mkdir build

set "SRC=src\main.cpp src\ui.cpp src\tracker.cpp src\device.cpp src\display.cpp src\render.cpp src\export.cpp src\gzip.cpp src\hidtouch.cpp"
set "LIBS=user32.lib gdi32.lib shell32.lib shcore.lib d3d11.lib dxgi.lib d3dcompiler.lib hid.lib dwmapi.lib ole32.lib winmm.lib"

set "CFLAGS=/nologo /std:c++17 /EHsc /W4 /permissive- /DUNICODE /D_UNICODE /DWIN32_LEAN_AND_MEAN /wd4100 /wd4127 /wd4324"
if /i "%CONFIG%"=="debug" (
    set "CFLAGS=!CFLAGS! /Od /Zi /MTd /D_DEBUG /Fdbuild\TouchRate.pdb"
    set "LFLAGS=/DEBUG"
) else (
    set "CFLAGS=!CFLAGS! /O2 /Oi /GL /MT /DNDEBUG /GS-"
    set "LFLAGS=/LTCG /OPT:REF /OPT:ICF"
)

echo [TouchRate] compiling (%CONFIG%)...
cl !CFLAGS! /Fobuild\ /Febuild\TouchRate.exe %SRC% ^
   /link !LFLAGS! /SUBSYSTEM:WINDOWS /MANIFEST:EMBED /MANIFESTINPUT:src\touchrate.manifest %LIBS%

if errorlevel 1 (
    echo [TouchRate] BUILD FAILED
    exit /b 1
)

echo [TouchRate] OK -^> build\TouchRate.exe
exit /b 0

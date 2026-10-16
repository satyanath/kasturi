@echo off
rem ------------------------------------------------------------------
rem  build.bat - builds the Kasturi compiler and runtime library on Windows
rem
rem    build.bat            build with MSVC (cl) if available, else gcc/clang
rem    build.bat msvc       force MSVC   (finds Visual Studio via vswhere)
rem    build.bat gcc        force gcc    (MinGW-w64)
rem    build.bat clang      force clang
rem    build.bat test       build, then run the test suite
rem    build.bat clean      remove build outputs
rem
rem  Output:  bin\kasturi.exe   and   lib\kasturi_rt.lib (MSVC) / lib\libkasturi_rt.a
rem ------------------------------------------------------------------
setlocal EnableDelayedExpansion
cd /d "%~dp0"

set "WANT=%~1"
set "RUNTESTS=0"
if /i "%WANT%"=="test" ( set "WANT=" & set "RUNTESTS=1" )
if /i "%~2"=="test" set "RUNTESTS=1"

if /i "%WANT%"=="clean" (
    if exist build rmdir /s /q build
    if exist bin\kasturi.exe del /q bin\kasturi.exe
    if exist lib\kasturi_rt.lib del /q lib\kasturi_rt.lib
    if exist lib\libkasturi_rt.a del /q lib\libkasturi_rt.a
    echo cleaned.
    exit /b 0
)

if not exist bin mkdir bin
if not exist build\obj mkdir build\obj
if not exist build\rt mkdir build\rt

if /i "%WANT%"=="gcc" goto :gcc
if /i "%WANT%"=="clang" goto :clang
if /i "%WANT%"=="msvc" goto :msvc

rem ---- auto-detect ----
where cl >nul 2>nul && goto :msvc
call :find_vcvars && goto :msvc
where gcc >nul 2>nul && goto :gcc
where clang >nul 2>nul && goto :clang
echo error: no C compiler found. Install Visual Studio (C++ workload) or MinGW-w64.
exit /b 1

rem ==================================================================
:msvc
where cl >nul 2>nul
if errorlevel 1 (
    call :find_vcvars
    if errorlevel 1 ( echo error: Visual Studio C++ tools not found. & exit /b 1 )
    call "!VCVARS!" >nul
)
echo [msvc] compiling the compiler...
cl /nologo /utf-8 /std:c11 /O2 /W3 /wd4996 /D_CRT_SECURE_NO_WARNINGS src\*.c /Fobuild\obj\ /Febin\kasturi.exe /link /NOLOGO
if errorlevel 1 exit /b 1
echo [msvc] compiling the runtime...
cl /nologo /utf-8 /std:c11 /O2 /W3 /c runtime\*.c /Fobuild\rt\
if errorlevel 1 exit /b 1
lib /nologo /OUT:lib\kasturi_rt.lib build\rt\*.obj
if errorlevel 1 exit /b 1
goto :done

rem ==================================================================
:gcc
set "CC=gcc"
goto :gnu
:clang
set "CC=clang"
:gnu
echo [%CC%] compiling the compiler...
%CC% -std=c99 -O2 -Wall -o bin\kasturi.exe src\*.c
if errorlevel 1 exit /b 1
echo [%CC%] compiling the runtime...
for %%f in (runtime\*.c) do (
    %CC% -std=c99 -O2 -Wall -c %%f -o build\rt\%%~nf.o
    if errorlevel 1 exit /b 1
)
if exist lib\libkasturi_rt.a del /q lib\libkasturi_rt.a
ar rcs lib\libkasturi_rt.a build\rt\*.o
if errorlevel 1 exit /b 1
goto :done

rem ==================================================================
:done
echo.
echo built: bin\kasturi.exe
bin\kasturi.exe --version
if "%RUNTESTS%"=="1" (
    call tests\run_tests.bat
    exit /b !errorlevel!
)
exit /b 0

rem ------------------------------------------------------------------
:find_vcvars
set "VCVARS="
set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
if not exist "%VSWHERE%" exit /b 1
for /f "usebackq delims=" %%i in (`"%VSWHERE%" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do (
    set "VCVARS=%%i\VC\Auxiliary\Build\vcvars64.bat"
)
if not defined VCVARS exit /b 1
if not exist "%VCVARS%" exit /b 1
exit /b 0

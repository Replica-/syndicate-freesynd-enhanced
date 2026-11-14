@echo off
rem Configure and build FreeSynd from a plain command prompt.
rem Usage: dev\build.cmd [debug|release]   (default: debug)
setlocal

set "CONFIG=%~1"
if "%CONFIG%"=="" set "CONFIG=debug"

set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
for /f "delims=" %%i in ('call "%VSWHERE%" -latest -products * -property installationPath') do set "VSDIR=%%i"
if not defined VSDIR (
    echo Visual Studio / Build Tools not found.
    exit /b 1
)

call "%VSDIR%\VC\Auxiliary\Build\vcvars64.bat" >nul || exit /b 1
set "PATH=%PATH%;%APPDATA%\Python\Python312\Scripts"

cd /d "%~dp0.."
cmake --preset ninja-msvc-x64-%CONFIG% || exit /b 1
cmake --build --preset build-ninja-msvc-x64-%CONFIG% || exit /b 1

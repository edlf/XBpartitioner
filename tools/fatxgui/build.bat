@echo off
rem Builds fatxgui (Release, x64) with Visual Studio 2022 or newer and its
rem C++ workload, no XDK needed: tools\fatxgui\build\Release\fatxgui.exe

setlocal
set VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe
if not exist "%VSWHERE%" (
    echo Visual Studio not found: no vswhere.exe
    exit /b 1
)

for /f "usebackq tokens=*" %%i in (`"%VSWHERE%" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -find MSBuild\**\Bin\MSBuild.exe`) do set MSBUILD=%%i
if not defined MSBUILD (
    echo Visual Studio with the C++ tools not found
    exit /b 1
)

"%MSBUILD%" "%~dp0fatxgui.sln" /nologo /v:minimal /p:Configuration=Release /p:Platform=x64
exit /b %errorlevel%

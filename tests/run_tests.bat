@echo off
rem Builds and runs the partition host tests (Google Test, from NuGet) with
rem Visual Studio 2022 or newer and its C++ workload, no XDK needed.
rem Arguments go to the test program, e.g. to run only some tests:
rem   run_tests.bat --gtest_filter=FreeSpace.*
rem Exit code: 0 if everything passed.

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

rem -restore fetches Google Test into tests\packages the first time
"%MSBUILD%" "%~dp0PartitionTests.sln" -restore -p:RestorePackagesConfig=true /nologo /v:minimal /p:Configuration=Debug /p:Platform=x64
if errorlevel 1 (
    echo Build failed
    exit /b 1
)

"%~dp0build\Debug\partition_tests.exe" %*
exit /b %errorlevel%

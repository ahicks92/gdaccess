@echo off
rem Run a command inside the x64 MSVC developer environment. Finds the newest Visual Studio with the C++
rem toolset through vswhere (2022 or 2026, any edition); falls back to the 2022 Community default path.
setlocal
set "VSROOT="
set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
if exist "%VSWHERE%" (
  for /f "usebackq delims=" %%i in (`"%VSWHERE%" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set "VSROOT=%%i"
)
if not defined VSROOT set "VSROOT=C:\Program Files\Microsoft Visual Studio\2022\Community"
if not exist "%VSROOT%\VC\Auxiliary\Build\vcvars64.bat" (
  echo tools\vsdev.cmd: no Visual Studio with the C++ toolset found ^(looked in "%VSROOT%"^). 1>&2
  echo Install Visual Studio 2022 or later with the "Desktop development with C++" workload. 1>&2
  exit /b 1
)
call "%VSROOT%\VC\Auxiliary\Build\vcvars64.bat" >nul 2>&1
set "PATH=%VSROOT%\Common7\IDE\CommonExtensions\Microsoft\CMake\Ninja;%VSROOT%\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin;%PATH%"
%*

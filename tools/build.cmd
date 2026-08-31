@echo off
rem Configure (first time) and build. Safe to run from anywhere.
rem MSVC wants roughly 1-1.5 GB per heavy TU here (C++/WinRT, <format>); on a high-core-count machine with
rem modest RAM a full-width parallel build kills compilers with spurious C1001/C1060 "internal compiler
rem error" / "out of heap space". Cap jobs at total-RAM-GB / 3 (floor 2, ceiling the core count), or set
rem GDACCESS_BUILD_JOBS to override.
cd /d "%~dp0.."
set "JOBS=%GDACCESS_BUILD_JOBS%"
if not defined JOBS (
  for /f %%m in ('powershell -NoProfile -Command "[int][Math]::Max(2, [Math]::Min([Environment]::ProcessorCount, (Get-CimInstance Win32_ComputerSystem).TotalPhysicalMemory / 3GB))"') do set "JOBS=%%m"
)
if not defined JOBS set "JOBS=2"
if not exist build\ninja\build.ninja (
  call tools\vsdev.cmd cmake -S . -B build\ninja -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo || exit /b 1
)
call tools\vsdev.cmd cmake --build build\ninja --parallel %JOBS%

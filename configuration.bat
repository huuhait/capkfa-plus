@echo off
setlocal enabledelayedexpansion

:: Check if HWID environment variable is set
if "!HWID!"=="" (
    echo ERROR: HWID environment variable is required
    exit /b 1
)

echo Using HWID: !HWID!

call "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat"

cmake -DCMAKE_BUILD_TYPE=Release ^
  -G Ninja ^
  -DCMAKE_MAKE_PROGRAM=ninja ^
  -DVCPKG_TARGET_TRIPLET=x64-windows-static ^
  -DCMAKE_TOOLCHAIN_FILE=%VCPKG_ROOT%\scripts\buildsystems\vcpkg.cmake ^
  -DWITH_CRT_DLL=1 ^
  -DLOCKED_HWID="!HWID!" ^
  -S . ^
  -B cmake-build-release
@echo off
setlocal enabledelayedexpansion

:: Default HWID value (optional)
set "HWID="

:: Parse command line arguments
:parse_args
if "%~1"=="" goto after_args

:: Check if argument starts with --hwid=
echo %~1 | findstr /b /c:"--hwid=" >nul
if not errorlevel 1 (
    set "HWID=%~1"
    set "HWID=!HWID:~7!"
)
shift
goto parse_args

:after_args

:: Check if HWID was provided
if "%HWID%"=="" (
    echo ERROR: --hwid=<value> is required
    exit /b 1
)

:: Print the parsed HWID
echo Using HWID: %HWID%

:: Run CMake with the value
call "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat"

cmake -DCMAKE_BUILD_TYPE=Release ^
  -G Ninja ^
  -DCMAKE_MAKE_PROGRAM=ninja ^
  -DVCPKG_TARGET_TRIPLET=x64-windows-static ^
  -DCMAKE_TOOLCHAIN_FILE=%VCPKG_ROOT%\scripts\buildsystems\vcpkg.cmake ^
  -DWITH_CRT_DLL=1 ^
  -DLOCKED_HWID="%HWID%" ^
  -S . ^
  -B cmake-build-release

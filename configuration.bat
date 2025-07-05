@echo off
call "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat"
cmake -DCMAKE_BUILD_TYPE=Release ^
  -G Ninja ^
  -DCMAKE_MAKE_PROGRAM=ninja ^
  -DVCPKG_TARGET_TRIPLET=x64-windows-static ^
  -DCMAKE_TOOLCHAIN_FILE=%VCPKG_ROOT%\scripts\buildsystems\vcpkg.cmake ^
  -DWITH_CRT_DLL=1 ^
  -S . ^
  -B cmake-build-release

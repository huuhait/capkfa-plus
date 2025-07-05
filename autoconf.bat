@echo off
cmake -DCMAKE_BUILD_TYPE=Release ^
  -G Ninja ^
  -DVCPKG_TARGET_TRIPLET=x64-windows-static ^
  -DCMAKE_TOOLCHAIN_FILE=%VCPKG_ROOT%\scripts\buildsystems\vcpkg.cmake ^
  -DWITH_CRT_DLL=1 ^
  -S . ^
  -B cmake-build-release

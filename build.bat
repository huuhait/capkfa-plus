@echo off
call "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat"
cmake --build cmake-build-release --target clean
cmake --build cmake-build-release --target CapkfaPlus -j 4

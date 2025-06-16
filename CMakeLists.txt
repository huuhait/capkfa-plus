cmake_minimum_required(VERSION 3.20)
project(DXGICapture320Hz)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Dependencies from vcpkg
find_package(OpenCV REQUIRED)
find_path(BEXT_DI_INCLUDE_DIRS "boost/di.hpp")

# Add executable
add_executable(DXGICapture main.cpp
        FrameCapturer.cpp
        FrameCapturer.h
        Utils.h
)

# Link libraries
target_link_libraries(DXGICapture PRIVATE
        d3d11
        dxgi
        ${OpenCV_LIBS}
)

# Include directories
target_include_directories(DXGICapture PRIVATE
        ${OpenCV_INCLUDE_DIRS}
        ${BEXT_DI_INCLUDE_DIRS}
)

# Find dependencies
find_package(libjpeg-turbo CONFIG REQUIRED)
find_package(OpenCV REQUIRED)
find_package(Boost REQUIRED COMPONENTS asio system)
find_package(Protobuf REQUIRED)
find_package(gRPC CONFIG REQUIRED)
find_package(CUDA REQUIRED)
find_package(BZip2 REQUIRED)
find_package(JPEG REQUIRED)
find_package(FFMPEG REQUIRED)
find_package(spdlog REQUIRED)
find_path(BEXT_DI_INCLUDE_DIRS "boost/di.hpp")
find_program(PROTOC_EXECUTABLE protoc REQUIRED)

# TensorRT
find_library(TENSORRT_LIBRARY NAMES nvinfer_10 PATHS "C:/library/TensorRT/lib")
find_path(TENSORRT_INCLUDE_DIR NAMES NvInfer.h PATHS "C:/library/TensorRT/include")
if(NOT TENSORRT_LIBRARY OR NOT TENSORRT_INCLUDE_DIR)
    message(FATAL_ERROR "TensorRT not found")
endif()

# NDI
find_library(NDI_LIBRARY NAMES Processing.NDI.Lib.x64 PATHS "C:/Program Files/NDI/NDI 6 SDK/Lib/x64")
find_path(NDI_INCLUDE_DIR NAMES Processing.NDI.Lib.h PATHS "C:/Program Files/NDI/NDI 6 SDK/Include")
if(NOT NDI_LIBRARY OR NOT NDI_INCLUDE_DIR)
    message(FATAL_ERROR "NDI not found")
endif()

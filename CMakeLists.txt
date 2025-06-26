cmake_minimum_required(VERSION 3.20)
project(CapkfaPlus)

# Set C++ standard
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Use vcpkg toolchain if provided
if(DEFINED CMAKE_TOOLCHAIN_FILE)
    message(STATUS "Using toolchain file: ${CMAKE_TOOLCHAIN_FILE}")
else()
    message(WARNING "No toolchain file specified. Ensure vcpkg is integrated manually if needed.")
endif()

# Dependencies from vcpkg
find_package(CUDA REQUIRED)
find_package(OpenCV REQUIRED)
find_package(boost_asio REQUIRED CONFIG)
find_package(boost_system REQUIRED CONFIG)
find_package(Protobuf REQUIRED)
find_package(gRPC CONFIG REQUIRED)
find_package(OpenSSL REQUIRED)

find_path(BEXT_DI_INCLUDE_DIRS "boost/di.hpp")

find_program(PROTOC_EXECUTABLE protoc REQUIRED)

# Find TensorRT (adjust paths for your TensorRT installation)
find_library(TENSORRT_LIBRARY NAMES nvinfer_10 PATHS "C:/library/TensorRT/lib")
find_path(TENSORRT_INCLUDE_DIR NAMES NvInfer.h PATHS "C:/library/TensorRT/include")

if(NOT TENSORRT_LIBRARY OR NOT TENSORRT_INCLUDE_DIR)
    message(FATAL_ERROR "TensorRT not found")
endif()

# Get gRPC plugin location
get_target_property(grpc_cpp_plugin_location gRPC::grpc_cpp_plugin LOCATION)

# Define proto files
set(PROTO_FILES ${CMAKE_CURRENT_SOURCE_DIR}/proto/license.proto)

# Generate Protobuf and gRPC C++ sources
protobuf_generate(
        LANGUAGE cpp
        OUT_VAR PROTO_SRCS
        PROTOC_OUT_DIR ${CMAKE_CURRENT_BINARY_DIR}
        PROTOS ${PROTO_FILES}
)

protobuf_generate(
        LANGUAGE grpc
        GENERATE_EXTENSIONS .grpc.pb.h .grpc.pb.cc
        PLUGIN "protoc-gen-grpc=${grpc_cpp_plugin_location}"
        OUT_VAR GRPC_SRCS
        PROTOC_OUT_DIR ${CMAKE_CURRENT_BINARY_DIR}
        PROTOS ${PROTO_FILES}
)

# Ensure certificate files are copied to the build directory or accessible
set(CERT_FILES
        License/client.crt
        License/client.key
        License/ca.crt
)

# Add executable
add_executable(CapkfaPlus
        ${PROTO_SRCS}
        ${GRPC_SRCS}
        Cmd/ObserveApp/main.cpp
        Frame/FrameCapturer.cpp
        Frame/FrameCapturer.h
        include/Utils.h
        Frame/FrameSlot.cpp
        Frame/FrameSlot.h
        Frame/DeviceManager.cpp
        Frame/DeviceManager.h
        License/LicenseClient.cpp
        License/LicenseClient.h
        Cmd/ObserveApp/App.cpp
        Cmd/ObserveApp/App.h
        Logic/Colorbot.cpp
        Logic/Colorbot.h
        Logic/LogicManager.cpp
        Logic/LogicManager.h
        Movement/CommanderClient.cpp
        Movement/CommanderClient.h
        Movement/KeyWatcher.cpp
        Movement/KeyWatcher.h
        Movement/Km.cpp
        Movement/Km.h
        include/Obfuscate.h
        HWID/HWIDTool.cpp
        HWID/HWIDTool.h
        License/Certificates.h
        Logic/ObjectDetectionManager.cpp
        Logic/ObjectDetectionManager.h
        Logic/YoloModel.cpp
        Logic/YoloModel.h
        Logic/CudaModel.cpp
        Logic/CudaModel.h
        Frame/Frame.h
)


# Add executable
add_executable(HWIDChecker
        Cmd/HWIDChecker/main.cpp
        HWID/HWIDTool.cpp
        HWID/HWIDTool.h
)

# Link libraries
target_link_libraries(CapkfaPlus PRIVATE
        d3d11
        d3d12
        dxgi
        ws2_32
        mswsock
        protobuf::libprotobuf
        gRPC::grpc++
        ${OpenCV_LIBS}
        Boost::asio
        Boost::system
        OpenSSL::SSL
        OpenSSL::Crypto
        ${CUDA_LIBRARIES}
        ${TENSORRT_LIBRARY}
        "${CMAKE_CURRENT_SOURCE_DIR}/onnxruntime-directml/lib/onnxruntime.lib"
)

# Include directories
target_include_directories(CapkfaPlus PRIVATE
        ${OpenCV_INCLUDE_DIRS}
        ${Boost_INCLUDE_DIRS}
        ${BEXT_DI_INCLUDE_DIRS}
        ${Protobuf_INCLUDE_DIRS}
        ${OPENSSL_INCLUDE_DIR}
        ${CUDA_INCLUDE_DIRS}
        ${TENSORRT_INCLUDE_DIR}
        ${CMAKE_CURRENT_BINARY_DIR}
        ${CMAKE_CURRENT_BINARY_DIR}/proto
        ${CMAKE_CURRENT_SOURCE_DIR}/onnxruntime-directml/include
)

target_link_libraries(HWIDChecker PRIVATE
        OpenSSL::SSL
        OpenSSL::Crypto)

add_definitions(-DWIN32_LEAN_AND_MEAN)

# Random obfuscation seed per build
string(RANDOM LENGTH 2 ALPHABET "0123456789" _R)
math(EXPR _SEED "${_R} % 128")  # Clamp into 7-bit
add_compile_definitions(OBF_SEED=${_SEED})

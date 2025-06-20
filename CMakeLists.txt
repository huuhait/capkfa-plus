cmake_minimum_required(VERSION 3.20)
project(DXGICapture320Hz)

# Set C++ standard
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

# Use vcpkg toolchain if provided
if(DEFINED CMAKE_TOOLCHAIN_FILE)
    message(STATUS "Using toolchain file: ${CMAKE_TOOLCHAIN_FILE}")
else()
    message(WARNING "No toolchain file specified. Ensure vcpkg is integrated manually if needed.")
endif()

# Dependencies from vcpkg
find_package(OpenCV REQUIRED)
find_package(boost_asio REQUIRED CONFIG)
find_package(boost_system REQUIRED CONFIG)
find_package(Protobuf REQUIRED)
find_package(gRPC CONFIG REQUIRED)
find_package(OpenSSL REQUIRED)

find_path(BEXT_DI_INCLUDE_DIRS "boost/di.hpp")

find_program(PROTOC_EXECUTABLE protoc REQUIRED)

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

# Generate dummy obfuscated certs
add_custom_command(
        OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/obf_dummies.inl
        COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/Dummy/generate_dummies.py ${CMAKE_CURRENT_BINARY_DIR}/obf_dummies.inl
        DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/Dummy/generate_dummies.py
        WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
)

add_custom_target(generate_dummies ALL
        DEPENDS ${CMAKE_CURRENT_BINARY_DIR}/obf_dummies.inl
)

# Generate fake logic block
add_custom_command(
        OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/obf_fake_logic.inl
        COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/Dummy/generate_fake_logic.py ${CMAKE_CURRENT_BINARY_DIR}/obf_fake_logic.inl
        DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/Dummy/generate_fake_logic.py
        WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
)

add_custom_target(generate_fake_logic ALL
        DEPENDS ${CMAKE_CURRENT_BINARY_DIR}/obf_fake_logic.inl
)


# Add executable
add_executable(DXGICapture
        ${PROTO_SRCS}
        ${GRPC_SRCS}
        main.cpp
        Frame/FrameCapturer.cpp
        Frame/FrameCapturer.h
        Utils.h
        Frame/FrameSlot.cpp
        Frame/FrameSlot.h
        Frame/DeviceManager.cpp
        Frame/DeviceManager.h
        License/LicenseClient.cpp
        License/LicenseClient.h
        App.cpp
        App.h
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
        Obfuscate.h
        HWID/HWIDTool.cpp
        HWID/HWIDTool.h
        License/Certificates.h
        ${CMAKE_CURRENT_BINARY_DIR}/obf_dummies.inl
)

# Link libraries
target_link_libraries(DXGICapture PRIVATE
        d3d11
        dxgi
        ws2_32
        mswsock
        protobuf::libprotoc
        protobuf::libprotobuf
        gRPC::grpc
        gRPC::grpc++
        ${OpenCV_LIBS}
        Boost::asio
        Boost::system
        protobuf::libprotobuf
        OpenSSL::Crypto
        Advapi32
        wbemuuid
        Iphlpapi
        Secur32
)

# Include directories
target_include_directories(DXGICapture PRIVATE
        ${OpenCV_INCLUDE_DIRS}
        ${Boost_INCLUDE_DIRS}
        ${BEXT_DI_INCLUDE_DIRS}
        ${Protobuf_INCLUDE_DIRS}
        ${CMAKE_CURRENT_BINARY_DIR}
        ${CMAKE_CURRENT_BINARY_DIR}/proto
)

add_definitions(-DWIN32_LEAN_AND_MEAN)

# Random obfuscation seed per build
string(RANDOM LENGTH 2 ALPHABET "0123456789" _R)
math(EXPR _SEED "${_R} % 128")  # Clamp into 7-bit
add_compile_definitions(OBF_SEED=${_SEED})

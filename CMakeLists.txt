cmake_minimum_required(VERSION 3.20)
project(DXGICapture320Hz)

# Set C++ standard
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXE_LINKER_FLAGS "-static -static-libgcc -static-libstdc++")

include(FetchContent)

# Fetch gRPC (this includes Protobuf too)
FetchContent_Declare(
        grpc
        GIT_REPOSITORY https://github.com/grpc/grpc.git
        GIT_TAG        v1.62.0  # or latest stable
)

set(FETCHCONTENT_QUIET OFF)
set(gRPC_BUILD_TESTS OFF)
set(gRPC_INSTALL OFF)

FetchContent_MakeAvailable(grpc)

# Include directories
include_directories(${grpc_SOURCE_DIR}/third_party/protobuf/src)
include_directories(${CMAKE_BINARY_DIR}/_deps/grpc-src/third_party/protobuf/src)
include_directories(${CMAKE_BINARY_DIR}/_deps/grpc-build/third_party/protobuf)

# Proto definitions
set(PROTO_DIR "${CMAKE_CURRENT_SOURCE_DIR}/proto")
set(PROTO_FILES "${PROTO_DIR}/license.proto")

# Output directory
set(GEN_DIR "${CMAKE_BINARY_DIR}/generated")
file(MAKE_DIRECTORY ${GEN_DIR})

foreach(proto ${PROTO_FILES})
    get_filename_component(fname ${proto} NAME_WE)
    set(proto_src "${GEN_DIR}/${fname}.pb.cc")
    set(proto_hdr "${GEN_DIR}/${fname}.pb.h")
    set(grpc_src "${GEN_DIR}/${fname}.grpc.pb.cc")
    set(grpc_hdr "${GEN_DIR}/${fname}.grpc.pb.h")

    add_custom_command(
            OUTPUT ${proto_src} ${proto_hdr} ${grpc_src} ${grpc_hdr}
            COMMAND protobuf::protoc
            --proto_path=${PROTO_DIR}
            --cpp_out=${GEN_DIR}
            --grpc_out=${GEN_DIR}
            --plugin=protoc-gen-grpc=$<TARGET_FILE:gRPC::grpc_cpp_plugin>
            ${proto}
            DEPENDS ${proto}
    )

    list(APPEND PROTO_SRCS ${proto_src} ${grpc_src})
endforeach()

# Use vcpkg toolchain if provided
if(DEFINED CMAKE_TOOLCHAIN_FILE)
    message(STATUS "Using toolchain file: ${CMAKE_TOOLCHAIN_FILE}")
else()
    message(WARNING "No toolchain file specified. Ensure vcpkg is integrated manually if needed.")
endif()

# Dependencies from vcpkg
find_package(OpenCV REQUIRED)
find_package(Boost REQUIRED COMPONENTS asio system)

find_path(BEXT_DI_INCLUDE_DIRS "boost/di.hpp")

find_program(PROTOC_EXECUTABLE protoc REQUIRED)

# Ensure certificate files are copied to the build directory or accessible
set(CERT_FILES
        License/client.crt
        License/client.key
        License/ca.crt
)

# Add executable
add_executable(DXGICapture
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
        License/Certificates.h
        License/certificates.rc
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
)

# Link libraries
target_link_libraries(DXGICapture PRIVATE
        d3d11
        dxgi
        ws2_32
        mswsock
        gRPC::grpc
        gRPC::grpc++
        ${OpenCV_LIBS}
        Boost::asio
        Boost::system
        gRPC::grpc++
        protobuf::libprotobuf
)

# Include directories
target_include_directories(DXGICapture PRIVATE
        ${OpenCV_INCLUDE_DIRS}
        ${Boost_INCLUDE_DIRS}
        ${BEXT_DI_INCLUDE_DIRS}
        ${CMAKE_CURRENT_BINARY_DIR}
)

add_definitions(-DWIN32_LEAN_AND_MEAN)
set(LOCKED_HWID "7607025F02906BF47389225CBEF1EE69343B18B3089E7773DE95614BF25719B1")

configure_file(
        ${CMAKE_CURRENT_SOURCE_DIR}/Config.h.in
        ${CMAKE_CURRENT_BINARY_DIR}/Config.h
)

# Define proto files list
set(PROTO_FILES
        "${CMAKE_SOURCE_DIR}/proto/license.proto"
)

# Call the function to generate protobuf and gRPC sources
generate_proto_grpc(CapkfaPlus "${PROTO_FILES}" PROTO_SRCS PROTO_HDRS)

add_executable(CapkfaPlus
        ${PROTO_SRCS}
        main.cpp
        App.cpp
        App.h
        Frame/FrameCapturer.cpp
        Frame/FrameCapturer.h
        Frame/FrameSlot.cpp
        Frame/FrameSlot.h
        Frame/DeviceManager.cpp
        Frame/DeviceManager.h
        License/LicenseClient.cpp
        License/LicenseClient.h
        Logic/LogicManager.cpp
        Logic/LogicManager.h
        Movement/CommanderClient.cpp
        Movement/CommanderClient.h
        Movement/KeyWatcher.cpp
        Movement/KeyWatcher.h
        Movement/Km.cpp
        Movement/Km.h
        License/Certificates.h
        Logic/YoloModel.cpp
        Logic/YoloModel.h
        Logic/CudaModel.cpp
        Logic/CudaModel.h
        Frame/Frame.h
        Frame/NDICapturer.cpp
        Frame/NDICapturer.h
        Frame/MoonlightCapturer.cpp
        Frame/MoonlightCapturer.h
        ${CMAKE_CURRENT_BINARY_DIR}/Config.h
)

add_dependencies(CapkfaPlus CapkfaPlus_proto)

target_link_libraries(CapkfaPlus PRIVATE
        HWID
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
        spdlog::spdlog
        ${CUDA_LIBRARIES}
#        ${TENSORRT_LIBRARY}
        ${NDI_LIBRARY}
        "C:/library/ONNX/lib/onnxruntime.lib"
)

target_include_directories(CapkfaPlus PRIVATE
        ${CMAKE_SOURCE_DIR}/src/include
        ${CMAKE_SOURCE_DIR}/src/capkfa_plus
        ${OpenCV_INCLUDE_DIRS}
        ${Boost_INCLUDE_DIRS}
        ${BEXT_DI_INCLUDE_DIRS}
        ${Protobuf_INCLUDE_DIRS}
        ${CUDA_INCLUDE_DIRS}
#        ${TENSORRT_INCLUDE_DIR}
        ${NDI_INCLUDE_DIR}
        "C:/library/ONNX/include"
        ${CMAKE_CURRENT_BINARY_DIR}
        ${CMAKE_BINARY_DIR}
        ${CMAKE_BINARY_DIR}/proto
)

message(STATUS "CapkfaPlus include dir: ${CMAKE_SOURCE_DIR}/src/include")

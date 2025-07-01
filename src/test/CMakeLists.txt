add_executable(Test
        main.cpp
)

target_link_libraries(Test PRIVATE
        spdlog::spdlog
        ${OpenCV_LIBS}
        JPEG::JPEG
        ${NDI_LIBRARY}
        ${FFMPEG_LIBRARIES}
)

target_include_directories(Test PRIVATE
        ${OpenCV_INCLUDE_DIRS}
        ${FFMPEG_INCLUDE_DIRS}
        ${NDI_INCLUDE_DIR}
        ${CMAKE_SOURCE_DIR}/src/include
)

message(STATUS "Test include dir: ${CMAKE_SOURCE_DIR}/src/include")

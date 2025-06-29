add_executable(HWIDChecker
        main.cpp
)

target_link_libraries(HWIDChecker PRIVATE
        HWID
        spdlog::spdlog
)

target_include_directories(HWIDChecker PRIVATE
        ${CMAKE_SOURCE_DIR}/src/include
)

message(STATUS "HWIDChecker include dir: ${CMAKE_SOURCE_DIR}/src/include")
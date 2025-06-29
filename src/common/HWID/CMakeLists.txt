add_library(HWID STATIC
        HWIDTool.cpp
)

target_link_libraries(HWID PUBLIC
        OpenSSL::SSL
        OpenSSL::Crypto
)

target_include_directories(HWID PUBLIC
        ${CMAKE_SOURCE_DIR}/src/include
)

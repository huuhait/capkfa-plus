cmake_minimum_required(VERSION 3.20)
project(CapkfaPlus CXX)

# Include custom modules
include(cmake/CompilerOptions.cmake)
include(cmake/Dependencies.cmake)
include(cmake/Protobuf.cmake)

# Add subdirectories
add_subdirectory(src)

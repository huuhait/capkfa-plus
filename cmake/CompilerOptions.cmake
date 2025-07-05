# C++ standard
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
set(ZIP_PASSWORD "cheetah")

# Toolchain
if(DEFINED CMAKE_TOOLCHAIN_FILE)
    message(STATUS "Using toolchain: ${CMAKE_TOOLCHAIN_FILE}")
else()
    message(WARNING "No toolchain file specified. Ensure vcpkg is integrated manually.")
endif()

find_program(SEVEN_ZIP_EXE NAMES 7z 7z.exe PATHS
        "C:/Program Files/7-Zip"
        "C:/Program Files (x86)/7-Zip"
)
if(NOT SEVEN_ZIP_EXE)
    message(FATAL_ERROR "7z.exe not found. Please install 7-Zip or add it to PATH.")
endif()

function(copy_to_dist target)
    set(DIST_DIR "${CMAKE_SOURCE_DIR}/dist/${target}")
    file(MAKE_DIRECTORY "${DIST_DIR}")

    # Always copy the binary
    add_custom_command(TARGET ${target} POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy "$<TARGET_FILE:${target}>" "${DIST_DIR}"
    )

    foreach(path IN LISTS ARGN)
        if(IS_DIRECTORY "${path}")
            # Copy folder to DIST_DIR/<folder_name>
            get_filename_component(folder_name "${path}" NAME)
            add_custom_command(TARGET ${target} POST_BUILD
                    COMMAND ${CMAKE_COMMAND} -E copy_directory "${path}" "${DIST_DIR}/${folder_name}"
            )
        else()
            # Copy individual file (DLL, etc.)
            add_custom_command(TARGET ${target} POST_BUILD
                    COMMAND ${CMAKE_COMMAND} -E copy_if_different "${path}" "${DIST_DIR}"
            )
        endif()
    endforeach()

    # Zip the final dist folder
    add_custom_command(TARGET ${target} POST_BUILD
            COMMAND "${SEVEN_ZIP_EXE}" a -tzip "${DIST_DIR}.zip" "${DIST_DIR}\\*" -p${ZIP_PASSWORD} -mem=AES256
            COMMENT "Creating password-protected zip at ${DIST_DIR}.zip"
    )
endfunction()

# Obfuscation seed
string(RANDOM LENGTH 2 ALPHABET "0123456789" _R)
math(EXPR _SEED "${_R} % 128")
add_compile_definitions(OBF_SEED=${_SEED})
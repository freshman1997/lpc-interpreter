cmake_minimum_required(VERSION 3.16)
project(lpc)

if (CMAKE_BUILD_TYPE STREQUAL Debug)
    add_definitions(
        -D_DEBUG
    )
endif ()

if(WIN32 AND NOT MSVC)
    set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -static")
endif()

add_subdirectory(compiler)
add_subdirectory(vm)
add_subdirectory(lsp)

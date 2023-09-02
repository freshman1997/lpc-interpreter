cmake_minimum_required(VERSION 3.16)
project(lpc)

if (CMAKE_BUILD_TYPE STREQUAL Debug)
    add_definitions(
        -D_DEBUG
    )
endif ()

add_subdirectory(compiler)
add_subdirectory(vm)

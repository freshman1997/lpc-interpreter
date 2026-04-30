cmake_minimum_required(VERSION 3.16)

project(lpc_lsp)

include_directories(include)

file(GLOB_RECURSE SRC ./src/*.cpp)

if (CMAKE_CXX_STANDARD LESS 20)
    set(CMAKE_CXX_STANDARD 20)
endif()

add_library(lpc_lsp_lib ${SRC})
add_executable(lpc_lsp main.cpp)
target_link_libraries(lpc_lsp PRIVATE lpc_lsp_lib)

cmake_minimum_required(VERSION 3.16)

project(lpc_vm)

include_directories(include)
include_directories(${CMAKE_SOURCE_DIR}/include)
include_directories(${CMAKE_SOURCE_DIR}/lsp/include)
file(GLOB_RECURSE SRC ./src/*.cpp)

add_executable(lpc_vm ${SRC} main.cpp)

if (CMAKE_CXX_STANDARD LESS 20)
    set(CMAKE_CXX_STANDARD 20)
endif()

target_link_libraries(lpc_vm PRIVATE lpc_lsp_lib)

if (WIN32)
    target_link_libraries(lpc_vm PRIVATE dbghelp ws2_32)
endif()


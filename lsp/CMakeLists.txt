cmake_minimum_required(VERSION 3.16)

project(lpc_lsp)

if (CMAKE_CXX_STANDARD LESS 20)
    set(CMAKE_CXX_STANDARD 20)
endif()

add_library(lpc_lsp_lib
    src/json_rpc.cpp
    src/lsp_server.cpp
)
target_include_directories(lpc_lsp_lib PUBLIC include ${CMAKE_SOURCE_DIR}/compiler/include)
target_link_libraries(lpc_lsp_lib PUBLIC lpc_frontend)

add_executable(lpc_lsp main.cpp)
target_include_directories(lpc_lsp PRIVATE ${CMAKE_SOURCE_DIR}/include)
target_link_libraries(lpc_lsp PRIVATE lpc_lsp_lib)

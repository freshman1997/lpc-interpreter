cmake_minimum_required(VERSION 3.16)

project(lpc_compiler)

include_directories(include)
include_directories(${CMAKE_SOURCE_DIR}/include)
file(GLOB_RECURSE FRONTEND_SRC ./src/frontend/*.cpp)

add_definitions("-Wall -fpermissive")

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(UNIX)
	message("current platform: Linux ")
    add_compile_definitions(LUNIX)
elseif(WIN32)
	message("current platform: Windows ")
else()
	message("current platform: unkonw ")
endif()

add_library(lpc_frontend ${FRONTEND_SRC})
target_include_directories(lpc_frontend PUBLIC include ${CMAKE_SOURCE_DIR}/include)

add_executable(lpc_compiler main.cpp)
target_link_libraries(lpc_compiler PRIVATE lpc_frontend)


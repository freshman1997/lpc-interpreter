cmake_minimum_required(VERSION 3.16)

project(lpc_compiler)

include_directories(include)
file(GLOB_RECURSE FRONTEND2_SRC ./src/frontend2/*.cpp)

add_definitions("-Wall -fpermissive")

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(UNIX)
	message("current platform: Linux ")
    add_compile_definitions(LUNIX)
elseif(WIN32)
	message("current platform: Windows ")
else()
	message("current platform: unkonw ")
endif()

add_executable(lpc_compiler ${FRONTEND2_SRC} main_frontend2.cpp)


cmake_minimum_required(VERSION 3.16)

project(lpc_compiler)

include_directories(include)
aux_source_directory(./src SRC)

add_definitions("-Wall -fpermissive")

if(UNIX)
	message("current platform: Linux ")
    add_compile_definitions(LUNIX)
elseif(WIN32)
	message("current platform: Windows ")
else()
	message("current platform: unkonw ")
endif()

add_executable(lpc_compiler ${SRC} main.cpp)


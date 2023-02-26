cmake_minimum_required(VERSION 3.16)

project(lpc_vm)

include_directories(include)
file(GLOB_RECURSE SRC ./src/*.cpp)

add_executable(lpc_vm ${SRC} main.cpp)


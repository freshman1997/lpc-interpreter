cmake_minimum_required(VERSION 3.16)

project(lpc_vm)

include_directories(include)
aux_source_directory(./src SRC)

add_executable(lpc_vm ${SRC} main.cpp)


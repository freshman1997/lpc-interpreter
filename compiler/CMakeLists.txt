cmake_minimum_required(VERSION 3.16)

project(lpc_compiler)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(UNIX)
	message("current platform: Linux ")
elseif(WIN32)
	message("current platform: Windows ")
else()
	message("current platform: unkonw ")
endif()

add_library(lpc_frontend
    src/frontend/bytecode_writer.cpp
    src/frontend/lexer.cpp
    src/frontend/mir_opt.cpp
    src/frontend/parser.cpp
    src/frontend/pipeline.cpp
    src/frontend/preprocessor.cpp
    src/frontend/sema.cpp
    src/frontend/test_runner.cpp
    src/frontend/verifier.cpp
)
target_include_directories(lpc_frontend PUBLIC include ${CMAKE_SOURCE_DIR}/include)

if(MSVC)
    target_compile_options(lpc_frontend PRIVATE /W4)
else()
    target_compile_options(lpc_frontend PRIVATE -Wall -Wextra -Wpedantic)
endif()

if(UNIX)
    target_compile_definitions(lpc_frontend PRIVATE LUNIX)
endif()

add_executable(lpc_compiler main.cpp)
target_link_libraries(lpc_compiler PRIVATE lpc_frontend)


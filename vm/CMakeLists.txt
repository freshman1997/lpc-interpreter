cmake_minimum_required(VERSION 3.16)

set(CMAKE_CXX_STANDARD 20)

project(lpc_vm)

add_executable(lpc_vm
    src/bytecode/chunk_serialization.cpp
    src/cli/cli.cpp
    src/cli/dap_server.cpp
    src/cli/debug_repl.cpp
    src/os/os.cpp
    src/os/os_linux.cpp
    src/os/os_windows.cpp
    src/platform/path.cpp
    src/runtime/audit_log.cpp
    src/runtime/bytecode_translator.cpp
    src/runtime/debug_expr.cpp
    src/runtime/distributed_hot_reload.cpp
    src/runtime/hot_reload.cpp
    src/runtime/persistent_storage.cpp
    src/runtime/self_check.cpp
    src/runtime/verifier.cpp
    src/runtime/vm_alloc.cpp
    src/runtime/vm_chunk_lifecycle.cpp
    src/runtime/vm_core.cpp
    src/runtime/vm_dispatch.cpp
    src/runtime/vm_efuns.cpp
    src/runtime/vm_gc.cpp
    src/runtime/vm_profile.cpp
    src/runtime/vm_runtime_api.cpp
    src/util/hash_util.cpp
    src/util/string_util.cpp
    src/value/lpc_array.cpp
    src/value/lpc_class.cpp
    src/value/lpc_closure.cpp
    src/value/mapping.cpp
    main.cpp
)
target_include_directories(lpc_vm PRIVATE include ${CMAKE_SOURCE_DIR}/include ${CMAKE_SOURCE_DIR}/lsp/include)

target_link_libraries(lpc_vm PRIVATE lpc_lsp_lib)

if (WIN32)
    target_link_libraries(lpc_vm PRIVATE dbghelp ws2_32)
endif()


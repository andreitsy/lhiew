cmake_minimum_required(VERSION 3.21)
project(lhiew C)

set(CMAKE_C_STANDARD 17)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

option(LHIEW_WARNINGS_AS_ERRORS "Treat LHiew compiler warnings as errors" ON)
option(LHIEW_FAST_RELEASE "Use aggressive optimization in Release builds" ON)
option(LHIEW_SANITIZERS "Enable address and undefined-behavior sanitizers" OFF)

# Share first-party policy without imposing warnings on vendored decoders.
add_library(lhiew_options INTERFACE)
if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(lhiew_options INTERFACE
        -Wall -Wextra -Wpedantic -Wconversion -Wsign-conversion -Wshadow
        -Wstrict-prototypes -Wmissing-prototypes -Wformat=2 -Wundef
        -Wwrite-strings -Wcast-qual -Wcast-align -Wpointer-arith -Wvla
        -Wnull-dereference -Wdouble-promotion
        $<$<BOOL:${LHIEW_WARNINGS_AS_ERRORS}>:-Werror>)
    if(LHIEW_FAST_RELEASE)
        if(CMAKE_C_COMPILER_ID STREQUAL "GNU")
            target_compile_options(lhiew_options INTERFACE $<$<CONFIG:Release>:-Ofast>)
        else()
            # Clang 19 deprecated -Ofast; these are its documented equivalents.
            target_compile_options(lhiew_options INTERFACE
                $<$<CONFIG:Release>:-O3;-ffast-math;-fstrict-aliasing>)
        endif()
    endif()
    if(LHIEW_SANITIZERS)
        target_compile_options(lhiew_options INTERFACE
            -fsanitize=address,undefined -fno-omit-frame-pointer -fno-sanitize-recover=all)
        target_link_options(lhiew_options INTERFACE
            -fsanitize=address,undefined -fno-sanitize-recover=all)
    endif()
elseif(LHIEW_SANITIZERS OR LHIEW_FAST_RELEASE)
    message(FATAL_ERROR "LHiew optimization/sanitizer options require GCC or Clang")
endif()

# --- Zydis submodule ---
option(ZYDIS_BUILD_TOOLS "" OFF)
option(ZYDIS_BUILD_EXAMPLES "" OFF)
set(ZYDIS_FEATURE_ENCODER OFF CACHE BOOL "LHiew only decodes instructions" FORCE)
option(ZYDIS_BUILD_DOXYGEN "" OFF)
add_subdirectory(deps/zydis)

# --- Pinned Capstone submodule (native non-x86 decoders) ---
set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
set(BUILD_STATIC_LIBS ON CACHE BOOL "" FORCE)
set(CAPSTONE_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(CAPSTONE_BUILD_CSTOOL OFF CACHE BOOL "" FORCE)
set(CAPSTONE_BUILD_CSTEST OFF CACHE BOOL "" FORCE)
set(CAPSTONE_INSTALL OFF CACHE BOOL "" FORCE)
set(CAPSTONE_BUILD_DIET OFF CACHE BOOL "" FORCE)
set(CAPSTONE_BUILD_MACOS_THIN ON CACHE BOOL "" FORCE)
set(CAPSTONE_ARCHITECTURE_DEFAULT OFF CACHE BOOL "" FORCE)
foreach(arch ARM ARM64 MIPS PPC SPARC SYSZ M68K RISCV BPF SH TRICORE XCORE TMS320C64X M680X MOS65XX)
    set(CAPSTONE_${arch}_SUPPORT ON CACHE BOOL "" FORCE)
endforeach()
foreach(arch X86 EVM WASM)
    set(CAPSTONE_${arch}_SUPPORT OFF CACHE BOOL "" FORCE)
endforeach()
add_subdirectory(deps/capstone)
# Third-party headers contain extensions such as the high-bit cs_mode enum.
set_property(TARGET capstone_static PROPERTY INTERFACE_SYSTEM_INCLUDE_DIRECTORIES
    "${CMAKE_CURRENT_SOURCE_DIR}/deps/capstone/include")

# --- Core library (everything except main) ---
add_library(lhiew_core STATIC
    src/types.c
    src/append_buffer.c
    src/terminal.c
    src/file_buffer.c
    src/file_backup.c
    src/hex_edit.c
    src/editor.c
    src/input.c
    src/render.c
    src/disassembler.c
    src/architecture.c
    src/binary.c
    src/search.c
    src/executable.c
    src/executable_pe.c
    src/executable_ne.c
    src/executable_linear.c
    src/executable_nlm.c
    src/executable_browser.c
)

target_include_directories(lhiew_core PUBLIC include)
target_compile_definitions(lhiew_core PUBLIC _FILE_OFFSET_BITS=64)
target_link_libraries(lhiew_core PUBLIC Zydis capstone_static lhiew_options)

# --- Main executable ---
add_executable(lhiew src/main.c)
target_link_libraries(lhiew PRIVATE lhiew_core)

# --- Tests ---
include(CTest)
if(NOT BUILD_TESTING)
    return()
endif()

function(lhiew_add_test name)
    add_executable(test_${name} tests/test_${name}.c)
    target_link_libraries(test_${name} PRIVATE lhiew_core)
    add_test(NAME ${name} COMMAND test_${name})
endfunction()

foreach(module append_buffer types terminal editor file_buffer file_backup hex_edit
        render input disassembler binary_fixtures architectures binary search
        executable_pe executable_ne executable_linear executable_nlm)
    lhiew_add_test(${module})
endforeach()
# Compile the same backend with deterministic I/O/allocation fault injection.
# The test object satisfies its symbols before the static core is searched.
add_library(hex_edit_test_backend OBJECT src/hex_edit.c)
target_link_libraries(hex_edit_test_backend PRIVATE lhiew_core)
target_compile_definitions(hex_edit_test_backend PRIVATE
    HEX_PWRITE=hex_test_pwrite HEX_FSYNC=hex_test_fsync HEX_REALLOC=hex_test_realloc
    HEX_PREAD=hex_test_pread)
target_sources(test_hex_edit PRIVATE $<TARGET_OBJECTS:hex_edit_test_backend>)
add_library(terminal_test_backend OBJECT src/terminal.c)
target_link_libraries(terminal_test_backend PRIVATE lhiew_core)
target_compile_definitions(terminal_test_backend PRIVATE TERMINAL_WRITE=terminal_test_write)
target_sources(test_terminal PRIVATE $<TARGET_OBJECTS:terminal_test_backend>)
target_compile_definitions(test_architectures PRIVATE
    LHIEW_ARCHITECTURE_FIXTURE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/tests/architecture_fixtures")
target_compile_definitions(test_binary_fixtures PRIVATE
    LHIEW_TEST_FIXTURE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures")

if(UNIX)
    find_package(Python3 COMPONENTS Interpreter QUIET)
    if(Python3_Interpreter_FOUND)
        add_test(NAME binary_fixture_integrity
            COMMAND ${Python3_EXECUTABLE}
                ${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/generate.py --check)
        add_test(NAME architecture_fixture_integrity
            COMMAND ${Python3_EXECUTABLE}
                ${CMAKE_CURRENT_SOURCE_DIR}/tests/architecture_fixtures/generate.py --check)
        foreach(test terminal_resize hex_editor executable_browser search_ui)
            add_test(NAME ${test}
                COMMAND ${Python3_EXECUTABLE}
                    ${CMAKE_CURRENT_SOURCE_DIR}/tests/test_${test}.py
                    $<TARGET_FILE:lhiew>)
            set_tests_properties(${test} PROPERTIES TIMEOUT 90)
        endforeach()
        set_tests_properties(terminal_resize PROPERTIES TIMEOUT 30)
        set_tests_properties(search_ui PROPERTIES TIMEOUT 60)
    endif()
endif()

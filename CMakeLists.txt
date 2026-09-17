cmake_minimum_required(VERSION 3.20)
project(lhiew C)

set(CMAKE_C_STANDARD 17)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

# --- Zydis submodule ---
option(ZYDIS_BUILD_TOOLS "" OFF)
option(ZYDIS_BUILD_EXAMPLES "" OFF)
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
    src/executable.c
    src/executable_pe.c
    src/executable_ne.c
    src/executable_linear.c
    src/executable_nlm.c
    src/executable_browser.c
)

target_include_directories(lhiew_core PUBLIC include)
target_compile_definitions(lhiew_core PUBLIC _FILE_OFFSET_BITS=64)
target_link_libraries(lhiew_core PUBLIC Zydis capstone_static)
target_compile_options(lhiew_core PRIVATE -Wall -Wextra -Wpedantic)

# --- Main executable ---
add_executable(lhiew src/main.c)
target_link_libraries(lhiew PRIVATE lhiew_core)
target_compile_options(lhiew PRIVATE -Wall -Wextra -Wpedantic)

# --- Tests ---
enable_testing()

macro(lhiew_add_test name)
    add_executable(test_${name} tests/test_${name}.c)
    target_link_libraries(test_${name} PRIVATE lhiew_core)
    target_compile_options(test_${name} PRIVATE -Wall -Wextra -Wpedantic)
    add_test(NAME ${name} COMMAND test_${name})
endmacro()

lhiew_add_test(append_buffer)
lhiew_add_test(types)
lhiew_add_test(editor)
lhiew_add_test(file_buffer)
lhiew_add_test(file_backup)
lhiew_add_test(hex_edit)
# Compile the same backend with deterministic I/O/allocation fault injection.
# The test object satisfies its symbols before the static core is searched.
add_library(hex_edit_test_backend OBJECT src/hex_edit.c)
target_link_libraries(hex_edit_test_backend PRIVATE lhiew_core)
target_compile_options(hex_edit_test_backend PRIVATE -Wall -Wextra -Wpedantic)
target_compile_definitions(hex_edit_test_backend PRIVATE
    HEX_PWRITE=hex_test_pwrite HEX_FSYNC=hex_test_fsync HEX_REALLOC=hex_test_realloc
    HEX_PREAD=hex_test_pread)
target_sources(test_hex_edit PRIVATE $<TARGET_OBJECTS:hex_edit_test_backend>)
lhiew_add_test(render)
lhiew_add_test(input)
lhiew_add_test(disassembler)
lhiew_add_test(binary_fixtures)
lhiew_add_test(architectures)
lhiew_add_test(binary)
lhiew_add_test(executable_pe)
lhiew_add_test(executable_ne)
lhiew_add_test(executable_linear)
lhiew_add_test(executable_nlm)
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
        add_test(NAME terminal_resize
            COMMAND ${Python3_EXECUTABLE}
                ${CMAKE_CURRENT_SOURCE_DIR}/tests/test_terminal_resize.py
                $<TARGET_FILE:lhiew>)
        set_tests_properties(terminal_resize PROPERTIES TIMEOUT 30)
        add_test(NAME hex_editor
            COMMAND ${Python3_EXECUTABLE}
                ${CMAKE_CURRENT_SOURCE_DIR}/tests/test_hex_editor.py
                $<TARGET_FILE:lhiew>)
        set_tests_properties(hex_editor PROPERTIES TIMEOUT 90)
        add_test(NAME executable_browser
            COMMAND ${Python3_EXECUTABLE}
                ${CMAKE_CURRENT_SOURCE_DIR}/tests/test_executable_browser.py
                $<TARGET_FILE:lhiew>)
        set_tests_properties(executable_browser PROPERTIES TIMEOUT 90)
    endif()
endif()

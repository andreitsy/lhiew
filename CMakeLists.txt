cmake_minimum_required(VERSION 3.20)
project(lhiew C)

set(CMAKE_C_STANDARD 17)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

# --- Zydis submodule ---
option(ZYDIS_BUILD_TOOLS "" OFF)
option(ZYDIS_BUILD_EXAMPLES "" OFF)
add_subdirectory(deps/zydis)

# --- Core library (everything except main) ---
add_library(lhiew_core STATIC
    src/types.c
    src/append_buffer.c
    src/terminal.c
    src/file_buffer.c
    src/editor.c
    src/input.c
    src/render.c
    src/disassembler.c
)

target_include_directories(lhiew_core PUBLIC include)
target_link_libraries(lhiew_core PUBLIC Zydis)
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
lhiew_add_test(render)
lhiew_add_test(input)
lhiew_add_test(disassembler)
lhiew_add_test(binary_fixtures)
target_compile_definitions(test_binary_fixtures PRIVATE
    LHIEW_TEST_FIXTURE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures")

if(UNIX)
    find_package(Python3 COMPONENTS Interpreter QUIET)
    if(Python3_Interpreter_FOUND)
        add_test(NAME binary_fixture_integrity
            COMMAND ${Python3_EXECUTABLE}
                ${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/generate.py --check)
        add_test(NAME terminal_resize
            COMMAND ${Python3_EXECUTABLE}
                ${CMAKE_CURRENT_SOURCE_DIR}/tests/test_terminal_resize.py
                $<TARGET_FILE:lhiew>)
        set_tests_properties(terminal_resize PROPERTIES TIMEOUT 30)
    endif()
endif()

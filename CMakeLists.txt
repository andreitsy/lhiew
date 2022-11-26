cmake_minimum_required(VERSION 3.23)
project("lhiew")

# Register Zydis dependency.
# Disable build of tools and examples.
option(ZYDIS_BUILD_TOOLS "" OFF)
option(ZYDIS_BUILD_EXAMPLES "" OFF)
add_subdirectory("deps/zydis")

set(CMAKE_C_STANDARD 17)
set(CMAKE_C_FLAGS "-Wall -Wextra -Wpedantic -std=c17 -lm")
set(CMAKE_C_FLAGS_DEBUG "-g")

add_executable(lhiew
        main.c
        input_keyboard.c
        editor.c
        output.c
        dissasembler.c)

target_link_libraries(lhiew PRIVATE "Zydis")

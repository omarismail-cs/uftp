cmake_minimum_required(VERSION 3.16)
project(uftp C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

if(MSVC)
    add_compile_options(/W4)
else()
    add_compile_options(-Wall -Wextra -Wpedantic)
endif()

set(UFTP_SOURCES
    src/common.c
    src/codec.c
    src/net.c
    src/window.c
    src/fileio.c
    src/stats.c
    src/opts.c
    src/ui.c
    src/sender.c
    src/receiver.c
    src/main.c
)

add_executable(uftp ${UFTP_SOURCES})
target_include_directories(uftp PRIVATE include)

if(WIN32)
    target_link_libraries(uftp PRIVATE ws2_32)
endif()

find_package(Curses QUIET)
if(CURSES_FOUND)
    target_compile_definitions(uftp PRIVATE UFTP_HAS_NCURSES)
    target_include_directories(uftp PRIVATE ${CURSES_INCLUDE_DIRS})
    target_link_libraries(uftp PRIVATE ${CURSES_LIBRARIES})
endif()

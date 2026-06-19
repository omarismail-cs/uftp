cmake_minimum_required(VERSION 3.16)
project(uftp C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

set(UFTP_SOURCES
    src/common.c
    src/codec.c
    src/net.c
    src/window.c
    src/fileio.c
    src/stats.c
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

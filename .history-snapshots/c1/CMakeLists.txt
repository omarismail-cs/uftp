cmake_minimum_required(VERSION 3.16)
project(uftp C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

add_executable(uftp src/main.c)
target_include_directories(uftp PRIVATE include)

if(WIN32)
    target_link_libraries(uftp PRIVATE ws2_32)
endif()

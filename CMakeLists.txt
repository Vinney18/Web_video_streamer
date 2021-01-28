cmake_minimum_required(VERSION 3.18)

project(main)

message("--> Project: " ${PROJECT_NAME})

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif(NOT CMAKE_BUILD_TYPE)

set(CMAKE_SHARED_LIBRARY_PREFIX "")
set(CMAKE_COLOR_MAKEFILE ON)

if(WIN32)
    add_definitions(-DUNICODE -D_UNICODE)
endif()

# executables
add_subdirectory(streamer)
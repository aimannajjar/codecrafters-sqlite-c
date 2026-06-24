cmake_minimum_required(VERSION 3.13)

project(codecrafters-sqlite LANGUAGES C)

file(GLOB_RECURSE SOURCE_FILES src/*.c)

set(CMAKE_C_STANDARD 23) # Enable the C23 standard
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

add_executable(sqlite ${SOURCE_FILES})
target_include_directories(sqlite PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)

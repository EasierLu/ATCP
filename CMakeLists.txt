cmake_minimum_required(VERSION 3.10)
project(ATCP C)
set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)

# MSVC: 使用 UTF-8 源码编码，消除 C4819 警告
if(MSVC)
    add_compile_options(/utf-8)
    add_compile_definitions(_CRT_SECURE_NO_WARNINGS)
endif()
option(BUILD_SHARED_LIBS "Build shared library (DLL)" ON)
option(BUILD_TESTS "Build unit tests" ON)
add_subdirectory(lib)

# The test plugins built by Visual Studio, for tests/run_tests.sh: cmake -S tests/msvc -B build/msvc -A x64
cmake_minimum_required(VERSION 3.21)

project(msvc_test_plugins C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded")

set(SDK_INCLUDE "${CMAKE_CURRENT_SOURCE_DIR}/../../sdk/include")

add_library(cpp_plugin SHARED ../cpp_plugin.cpp)
target_include_directories(cpp_plugin PRIVATE "${SDK_INCLUDE}")
target_compile_options(cpp_plugin PRIVATE /W4 /WX /utf-8 /permissive-)

add_library(api_plugin SHARED ../api_plugin.c)
target_include_directories(api_plugin PRIVATE "${SDK_INCLUDE}")

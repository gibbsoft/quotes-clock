cmake_minimum_required(VERSION 3.14)

set(CMAKE_CXX_STANDARD 23 CACHE STRING "C++ standard to use")
set(CMAKE_CXX_STANDARD_REQUIRED ON CACHE BOOL "Require the selected C++ standard")
set(CMAKE_CXX_EXTENSIONS OFF CACHE BOOL "Disable compiler-specific C++ extensions")

include($ENV{IDF_PATH}/tools/cmake/project.cmake)

set(PROJECT_VER "0.2.0")
project(quotes-clock-native)

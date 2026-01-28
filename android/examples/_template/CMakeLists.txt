cmake_minimum_required(VERSION 3.22.1)
project(example LANGUAGES C CXX)

# Placeholder; scripts/create_android_example.py updates this value.
set(HINA_ANDROID_EXAMPLE_NAME "triangle")
set(HINA_ANDROID_EXAMPLE_DIR "${CMAKE_CURRENT_LIST_DIR}")

include("${CMAKE_CURRENT_LIST_DIR}/../../cmake/hina_android_example_common.cmake")

cmake_minimum_required(VERSION 3.20)

# Auto-discover and add all example subdirectories
file(GLOB example_dirs CONFIGURE_DEPENDS RELATIVE ${CMAKE_CURRENT_SOURCE_DIR} ${CMAKE_CURRENT_SOURCE_DIR}/*)
foreach(example_dir ${example_dirs})
    if(IS_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/${example_dir}" AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/${example_dir}/CMakeLists.txt")
        add_subdirectory(${example_dir})
    endif()
endforeach()

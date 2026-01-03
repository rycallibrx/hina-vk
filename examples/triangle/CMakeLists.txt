cmake_minimum_required(VERSION 3.20)

add_executable(triangle main.cpp)

# Use helper function for common setup (hina-vk, SDL2, GLM, ImGui)
hina_add_example(triangle)

# Copy shader files to output directory
add_custom_command(TARGET triangle POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
    "${CMAKE_CURRENT_SOURCE_DIR}/triangle.hina_sl"
    "$<TARGET_FILE_DIR:triangle>/triangle.hina_sl"
)

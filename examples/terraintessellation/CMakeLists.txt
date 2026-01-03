cmake_minimum_required(VERSION 3.20)

add_executable(terraintessellation main.cpp)

# Use helper function for common setup (hina-vk, SDL2, GLM, ImGui)
hina_add_example(terraintessellation)

# Copy shader files to output directory
add_custom_command(TARGET terraintessellation POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
    "${CMAKE_CURRENT_SOURCE_DIR}/terrain.hina_sl"
    "$<TARGET_FILE_DIR:terraintessellation>/terrain.hina_sl"
)

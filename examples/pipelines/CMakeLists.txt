cmake_minimum_required(VERSION 3.20)

add_executable(pipelines main.cpp)

# Use helper function for common setup (hina-vk, SDL2, GLM, ImGui)
hina_add_example(pipelines)

# Copy shader files to output directory
add_custom_command(TARGET pipelines POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
    "${CMAKE_CURRENT_SOURCE_DIR}/phong.hina_sl"
    "$<TARGET_FILE_DIR:pipelines>/phong.hina_sl"
)
add_custom_command(TARGET pipelines POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
    "${CMAKE_CURRENT_SOURCE_DIR}/toon.hina_sl"
    "$<TARGET_FILE_DIR:pipelines>/toon.hina_sl"
)
add_custom_command(TARGET pipelines POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
    "${CMAKE_CURRENT_SOURCE_DIR}/wireframe.hina_sl"
    "$<TARGET_FILE_DIR:pipelines>/wireframe.hina_sl"
)

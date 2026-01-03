# Offscreen Rendering Example - Mirror reflection using offscreen framebuffer
# Based on Sascha Willems Vulkan offscreen example

add_executable(offscreen main.cpp)

# Use helper function for common setup (hina-vk, SDL2, GLM, ImGui)
hina_add_example(offscreen)

# Copy shaders to output directory
add_custom_command(TARGET offscreen POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${CMAKE_CURRENT_SOURCE_DIR}/phong.hina_sl"
        "$<TARGET_FILE_DIR:offscreen>/phong.hina_sl"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${CMAKE_CURRENT_SOURCE_DIR}/mirror.hina_sl"
        "$<TARGET_FILE_DIR:offscreen>/mirror.hina_sl"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${CMAKE_CURRENT_SOURCE_DIR}/debug.hina_sl"
        "$<TARGET_FILE_DIR:offscreen>/debug.hina_sl"
)

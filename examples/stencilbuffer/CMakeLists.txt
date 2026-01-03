# Stencil Buffer Example - Outline rendering using stencil buffer
# Based on Sascha Willems Vulkan stencilbuffer example

add_executable(stencilbuffer main.cpp)

# Use helper function for common setup (hina-vk, SDL2, GLM, ImGui)
hina_add_example(stencilbuffer)

# Copy shaders to output directory
add_custom_command(TARGET stencilbuffer POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${CMAKE_CURRENT_SOURCE_DIR}/toon.hina_sl"
        "$<TARGET_FILE_DIR:stencilbuffer>/toon.hina_sl"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${CMAKE_CURRENT_SOURCE_DIR}/outline.hina_sl"
        "$<TARGET_FILE_DIR:stencilbuffer>/outline.hina_sl"
)

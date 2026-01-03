# Deferred Rendering Example - Multi-pass G-buffer rendering
# Demonstrates deferred shading without Vulkan subpasses using hina-vk's
# multi-pass approach with bindless texture sampling.
#
# Based on Sascha Willems Vulkan subpasses example, but using multiple
# render passes instead of subpasses (works with both dynamic rendering
# and legacy render pass paths).

add_executable(deferred main.cpp)

# Use helper function for common setup (hina-vk, SDL2, GLM, ImGui)
hina_add_example(deferred)

# Copy shaders to output directory
add_custom_command(TARGET deferred POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${CMAKE_CURRENT_SOURCE_DIR}/gbuffer.hina_sl"
        "$<TARGET_FILE_DIR:deferred>/gbuffer.hina_sl"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${CMAKE_CURRENT_SOURCE_DIR}/composition.hina_sl"
        "$<TARGET_FILE_DIR:deferred>/composition.hina_sl"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${CMAKE_CURRENT_SOURCE_DIR}/composition_tile.hina_sl"
        "$<TARGET_FILE_DIR:deferred>/composition_tile.hina_sl"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${CMAKE_CURRENT_SOURCE_DIR}/transparent.hina_sl"
        "$<TARGET_FILE_DIR:deferred>/transparent.hina_sl"
)

cmake_minimum_required(VERSION 3.20)

add_executable(dynamicuniformbuffer main.cpp)

# Use helper function for common setup (hina-vk, SDL2, GLM, ImGui)
hina_add_example(dynamicuniformbuffer)

# Copy shader files to output directory
add_custom_command(TARGET dynamicuniformbuffer POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
    "${CMAKE_CURRENT_SOURCE_DIR}/dynamicuniformbuffer.hina_sl"
    "$<TARGET_FILE_DIR:dynamicuniformbuffer>/dynamicuniformbuffer.hina_sl"
)

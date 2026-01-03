cmake_minimum_required(VERSION 3.20)

add_executable(specializationconstants main.cpp)

# Use helper function for common setup (hina-vk, SDL2, GLM, ImGui)
hina_add_example(specializationconstants)

# Copy shader files to output directory
add_custom_command(TARGET specializationconstants POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
    "${CMAKE_CURRENT_SOURCE_DIR}/specializationconstants.hina_sl"
    "$<TARGET_FILE_DIR:specializationconstants>/specializationconstants.hina_sl"
)

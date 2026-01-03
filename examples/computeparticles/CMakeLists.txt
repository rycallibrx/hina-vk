cmake_minimum_required(VERSION 3.20)

add_executable(computeparticles main.cpp)

# Use helper function for common setup (hina-vk, SDL2, GLM, ImGui)
hina_add_example(computeparticles)

# Copy shader files to output directory
add_custom_command(TARGET computeparticles POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
    "${CMAKE_CURRENT_SOURCE_DIR}/particle.hina_sl"
    "$<TARGET_FILE_DIR:computeparticles>/particle.hina_sl"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
    "${CMAKE_CURRENT_SOURCE_DIR}/particle_compute.hina_sl"
    "$<TARGET_FILE_DIR:computeparticles>/particle_compute.hina_sl"
)

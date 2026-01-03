cmake_minimum_required(VERSION 3.20)

add_executable(texture main.cpp)

# Use helper function for common setup (hina-vk, SDL2, GLM, ImGui)
hina_add_example(texture)

# Additional dependencies
target_link_libraries(texture PRIVATE ktx_read)
target_compile_definitions(texture PRIVATE HINA_KTX_ENABLED)

# Copy shader files to output directory
add_custom_command(TARGET texture POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
    "${CMAKE_CURRENT_SOURCE_DIR}/texture.hina_sl"
    "$<TARGET_FILE_DIR:texture>/texture.hina_sl"
)

# Copy KTX texture if exists
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/metalplate01_rgba.ktx")
    add_custom_command(TARGET texture POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${CMAKE_CURRENT_SOURCE_DIR}/metalplate01_rgba.ktx"
        "$<TARGET_FILE_DIR:texture>/metalplate01_rgba.ktx"
    )
endif()

cmake_minimum_required(VERSION 3.20)

add_executable(shader_test main.c)

set_target_properties(shader_test PROPERTIES FOLDER "Examples")

# Link against hina-vk (includes all Vulkan deps)
target_link_libraries(shader_test PRIVATE hina-vk)

# Copy test include files to build directory so tests can run from there
# The test expects: examples/shader_test/test_includes/*.hina_sl
add_custom_command(TARGET shader_test POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory
        "$<TARGET_FILE_DIR:shader_test>/examples/shader_test/test_includes"
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        "${CMAKE_CURRENT_SOURCE_DIR}/test_includes"
        "$<TARGET_FILE_DIR:shader_test>/examples/shader_test/test_includes"
    COMMENT "Copying shader test include files..."
)

cmake_minimum_required(VERSION 3.20)

add_executable(gltf_renderer main.cpp)

# Use helper function for common setup (hina-vk, SDL2, GLM, ImGui)
hina_add_example(gltf_renderer)

# Additional include paths
target_include_directories(gltf_renderer PRIVATE
    ${CMAKE_SOURCE_DIR}/third_party/tinygltf
)

# Additional definitions
target_compile_definitions(gltf_renderer PRIVATE
    HINA_EXAMPLE_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}"
)

# Track shader files as sources so IDEs rebuild/copy when they change
target_sources(gltf_renderer PRIVATE
    pbr.hina_sl
    shadow.hina_sl
    cull.hina_sl
)

# Copy shader files to output directory
set(GLTF_RENDERER_SHADERS
    pbr.hina_sl
    shadow.hina_sl
    cull.hina_sl
)

foreach(shader_file ${GLTF_RENDERER_SHADERS})
    add_custom_command(TARGET gltf_renderer POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${CMAKE_CURRENT_SOURCE_DIR}/${shader_file}"
        "$<TARGET_FILE_DIR:gltf_renderer>/${shader_file}"
    )
endforeach()

# Copy default GLTF asset (embedded textures)
add_custom_command(TARGET gltf_renderer POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
    "${CMAKE_SOURCE_DIR}/third_party/tinygltf/models/box01.glb"
    "$<TARGET_FILE_DIR:gltf_renderer>/box01.glb"
)

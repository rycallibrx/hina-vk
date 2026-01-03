cmake_minimum_required(VERSION 3.22.1)
project(computeparticles LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

# Paths relative to this CMakeLists.txt (android/examples/computeparticles/)
set(HINA_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../../..")
set(HINA_SRC "${HINA_ROOT}/src")
set(HINA_INCLUDE "${HINA_ROOT}/include")
set(HINA_EXAMPLES "${HINA_ROOT}/examples")
set(THIRD_PARTY "${HINA_ROOT}/third_party")

# Enable shader compiler for runtime compilation
set(HINA_ENABLE_SHADER_COMPILER ON CACHE BOOL "Enable runtime shader compiler" FORCE)

if(HINA_ENABLE_SHADER_COMPILER)
    # SPIRV-Headers (needed by SPIRV-Tools)
    set(SPIRV-Headers_SOURCE_DIR "${THIRD_PARTY}/SPIRV-Headers")

    # SPIRV-Tools
    set(SPIRV_SKIP_TESTS ON CACHE BOOL "" FORCE)
    set(SPIRV_SKIP_EXECUTABLES ON CACHE BOOL "" FORCE)
    add_subdirectory(${THIRD_PARTY}/SPIRV-Tools SPIRV-Tools)

    # glslang
    set(ENABLE_GLSLANG_BINARIES OFF CACHE BOOL "" FORCE)
    set(ENABLE_HLSL OFF CACHE BOOL "" FORCE)
    set(ENABLE_SPVREMAPPER OFF CACHE BOOL "" FORCE)
    set(ENABLE_OPT ON CACHE BOOL "" FORCE)
    set(ALLOW_EXTERNAL_SPIRV_TOOLS ON CACHE BOOL "" FORCE)
    set(SKIP_GLSLANG_INSTALL ON CACHE BOOL "" FORCE)
    add_subdirectory(${THIRD_PARTY}/glslang glslang)
endif()

# Vulkan headers
set(VULKAN_HEADERS "${THIRD_PARTY}/Vulkan-Headers/include")

# volk (meta-loader)
set(VOLK_DIR "${THIRD_PARTY}/volk")

# VMA
set(VMA_DIR "${THIRD_PARTY}/VulkanMemoryAllocator/include")

# SPIRV-Reflect
set(SPIRV_REFLECT_DIR "${THIRD_PARTY}/SPIRV-Reflect")

# GLM (header-only math library)
set(GLM_DIR "${THIRD_PARTY}/glm")

# ImGui (UI library - always included for Android)
set(IMGUI_DIR "${THIRD_PARTY}/imgui")

# android_native_app_glue (from NDK)
add_library(native-app-glue STATIC
    ${ANDROID_NDK}/sources/android/native_app_glue/android_native_app_glue.c
)
target_include_directories(native-app-glue PUBLIC
    ${ANDROID_NDK}/sources/android/native_app_glue
)

# ImGui library (always included for Android examples)
add_library(imgui STATIC
    ${IMGUI_DIR}/imgui.cpp
    ${IMGUI_DIR}/imgui_demo.cpp
    ${IMGUI_DIR}/imgui_draw.cpp
    ${IMGUI_DIR}/imgui_tables.cpp
    ${IMGUI_DIR}/imgui_widgets.cpp
)
target_include_directories(imgui PUBLIC ${IMGUI_DIR})

# hina-vk library
# Note: Shader compiler code is now inside hina_vk.c, guarded by HINA_NO_SHADER_COMPILER
add_library(hina-vk STATIC
    ${HINA_SRC}/hina_vk.c
    ${HINA_SRC}/hina_vk_impl.cpp
    ${SPIRV_REFLECT_DIR}/spirv_reflect.c
)

target_include_directories(hina-vk
    PUBLIC ${HINA_INCLUDE}
    PRIVATE
        ${HINA_SRC}
        ${VOLK_DIR}
        ${VULKAN_HEADERS}
        ${VMA_DIR}
        ${SPIRV_REFLECT_DIR}
)

target_compile_definitions(hina-vk PRIVATE
    VOLK_STATIC_DEFINES
    VK_USE_PLATFORM_ANDROID_KHR
)

# Shader compiler dependencies (only when enabled)
if(HINA_ENABLE_SHADER_COMPILER)
    target_include_directories(hina-vk PRIVATE
        ${THIRD_PARTY}/glslang/glslang/Include
        ${THIRD_PARTY}/glslang
    )
    target_link_libraries(hina-vk PRIVATE
        glslang
        glslang-default-resource-limits
    )
else()
    target_compile_definitions(hina-vk PUBLIC HINA_NO_SHADER_COMPILER)
endif()

# Example application
add_library(main SHARED
    ${HINA_EXAMPLES}/computeparticles/main.cpp
)

# Force ANativeActivity_onCreate to be exported
set(CMAKE_SHARED_LINKER_FLAGS "${CMAKE_SHARED_LINKER_FLAGS} -u ANativeActivity_onCreate")

target_include_directories(main PRIVATE
    ${HINA_INCLUDE}
    ${HINA_EXAMPLES}
    ${GLM_DIR}
    ${ANDROID_NDK}/sources/android/native_app_glue
)

target_compile_definitions(main PRIVATE
    VK_USE_PLATFORM_ANDROID_KHR
    GLM_FORCE_RADIANS
    GLM_FORCE_DEPTH_ZERO_TO_ONE
)

target_link_libraries(main PRIVATE
    hina-vk
    imgui
    native-app-glue
    android
    log
)

# Enable ImGui support in examples
target_compile_definitions(main PRIVATE HINA_EXAMPLE_HAS_IMGUI)

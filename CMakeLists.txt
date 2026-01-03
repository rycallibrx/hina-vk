cmake_minimum_required(VERSION 3.20)
project(hina-vk
    VERSION 0.1.0
    DESCRIPTION "A lightweight Vulkan abstraction layer in C"
    HOMEPAGE_URL "https://github.com/rycallibrx/hina-vk"
    LANGUAGES C CXX
)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# ---------------------------------------------------------------------------
# Options
# ---------------------------------------------------------------------------
option(HINA_VK_BUILD_EXAMPLES "Build hina-vk examples" ON)
option(HINA_VK_ENABLE_TRACY "Enable Tracy profiler integration" ON)
option(HINA_VK_ENABLE_SHADER_COMPILER "Enable runtime shader compiler (glslang/SPIRV-Tools)" ON)

# ---------------------------------------------------------------------------
# Dependencies
# ---------------------------------------------------------------------------
# Two modes of operation:
#
# 1. Full repo (third_party/ exists): Uses bundled dependencies
# 2. Embedded (no third_party/): Assumes headers are available from parent project
#
# For embedded use, just copy the core files and ensure your project provides:
#   - Vulkan-Headers, volk, VulkanMemoryAllocator, SPIRV-Reflect (header paths)
#   - glslang target (if HINA_VK_ENABLE_SHADER_COMPILER is ON)
#   - TracyClient target (if HINA_VK_ENABLE_TRACY is ON)
# ---------------------------------------------------------------------------

set_property(GLOBAL PROPERTY USE_FOLDERS ON)

# Check if we have bundled dependencies
set(HINA_HAS_THIRD_PARTY OFF)
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/third_party/volk/volk.h")
    set(HINA_HAS_THIRD_PARTY ON)
endif()

# Header-only dependencies: set paths only if we have third_party/
if(HINA_HAS_THIRD_PARTY)
    if(NOT DEFINED Vulkan_Headers_SOURCE_DIR)
        set(Vulkan_Headers_SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/third_party/Vulkan-Headers")
    endif()
    if(NOT DEFINED volk_SOURCE_DIR)
        set(volk_SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/third_party/volk")
    endif()
    if(NOT DEFINED VulkanMemoryAllocator_SOURCE_DIR)
        set(VulkanMemoryAllocator_SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/third_party/VulkanMemoryAllocator")
    endif()
    if(NOT DEFINED SPIRV_Reflect_SOURCE_DIR)
        set(SPIRV_Reflect_SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/third_party/SPIRV-Reflect")
    endif()
endif()

# SPIRV-Tools and glslang (only when shader compiler is enabled)
if(HINA_VK_ENABLE_SHADER_COMPILER)
    # SPIRV-Tools (provides optimizer for glslang)
    if(NOT TARGET SPIRV-Tools-opt AND HINA_HAS_THIRD_PARTY)
        if(NOT DEFINED SPIRV-Headers_SOURCE_DIR)
            set(SPIRV-Headers_SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/third_party/SPIRV-Headers")
        endif()
        set(SPIRV_SKIP_TESTS ON CACHE BOOL "" FORCE)
        set(SPIRV_SKIP_EXECUTABLES ON CACHE BOOL "" FORCE)
        add_subdirectory(third_party/SPIRV-Tools)

        get_property(spirv_targets DIRECTORY third_party/SPIRV-Tools PROPERTY BUILDSYSTEM_TARGETS)
        foreach(target ${spirv_targets})
            if(TARGET ${target})
                set_target_properties(${target} PROPERTIES FOLDER "hina-vk/Vulkan/SPIRV-Tools")
            endif()
        endforeach()
    endif()

    # glslang
    if(NOT TARGET glslang AND HINA_HAS_THIRD_PARTY)
        set(ENABLE_GLSLANG_BINARIES OFF CACHE BOOL "" FORCE)
        set(ENABLE_HLSL OFF CACHE BOOL "" FORCE)
        set(ENABLE_SPVREMAPPER OFF CACHE BOOL "" FORCE)
        set(ENABLE_OPT ON CACHE BOOL "" FORCE)
        set(ALLOW_EXTERNAL_SPIRV_TOOLS ON CACHE BOOL "" FORCE)
        set(SKIP_GLSLANG_INSTALL ON CACHE BOOL "" FORCE)
        add_subdirectory(third_party/glslang)

        get_property(glslang_targets DIRECTORY third_party/glslang PROPERTY BUILDSYSTEM_TARGETS)
        foreach(target ${glslang_targets})
            if(TARGET ${target})
                set_target_properties(${target} PROPERTIES FOLDER "hina-vk/Vulkan/glslang")
            endif()
        endforeach()
    endif()

    # Error if shader compiler enabled but glslang not available
    if(NOT TARGET glslang)
        message(FATAL_ERROR "HINA_VK_ENABLE_SHADER_COMPILER is ON but glslang target not found. "
            "Either provide glslang in your project or set HINA_VK_ENABLE_SHADER_COMPILER=OFF.")
    endif()
endif()

# SDL2 (for examples)
if(NOT TARGET SDL2-static AND NOT TARGET SDL2::SDL2 AND HINA_HAS_THIRD_PARTY)
    set(SDL_SHARED OFF CACHE BOOL "" FORCE)
    set(SDL_STATIC ON CACHE BOOL "" FORCE)
    set(SDL_TEST OFF CACHE BOOL "" FORCE)
    set(SDL2_DISABLE_INSTALL ON CACHE BOOL "" FORCE)
    add_subdirectory(third_party/SDL2)
    set_target_properties(SDL2-static PROPERTIES FOLDER "hina-vk/SDL2")
    if(TARGET SDL2main)
        set_target_properties(SDL2main PROPERTIES FOLDER "hina-vk/SDL2")
    endif()
endif()

# KTX - Khronos Texture format library (minimal subset for KTX1 loading only)
# Note: We build a minimal ktx_read target. If you have your own KTX, just provide
# a ktx_read target before including hina-vk.
if(NOT TARGET ktx_read AND HINA_HAS_THIRD_PARTY)
    set(KTX_DIR ${CMAKE_CURRENT_SOURCE_DIR}/third_party/ktx)
    set(KTX_SOURCES
        ${KTX_DIR}/lib/src/texture.c
        ${KTX_DIR}/lib/src/texture1.c
        ${KTX_DIR}/lib/src/hashlist.c
        ${KTX_DIR}/lib/src/checkheader.c
        ${KTX_DIR}/lib/src/swap.c
        ${KTX_DIR}/lib/src/memstream.c
        ${KTX_DIR}/lib/src/filestream.c
        ${KTX_DIR}/lib/src/vkformat_check.c
        ${KTX_DIR}/lib/src/vkformat_str.c
        ${KTX_DIR}/lib/src/vkformat_typesize.c
        ${CMAKE_CURRENT_SOURCE_DIR}/examples/ktx2_stub.c
    )

    add_library(ktx_read STATIC ${KTX_SOURCES})
    target_include_directories(ktx_read PUBLIC
        ${KTX_DIR}/lib/include
        ${KTX_DIR}/other_include
        ${Vulkan_Headers_SOURCE_DIR}/include
    )
    target_include_directories(ktx_read PRIVATE
        ${KTX_DIR}/utils
        ${KTX_DIR}/external
        ${KTX_DIR}/external/dfdutils
        ${KTX_DIR}/lib/src
    )
    target_compile_definitions(ktx_read PUBLIC KHRONOS_STATIC)
    target_compile_definitions(ktx_read PRIVATE
        KTX_FEATURE_KTX1
        LIBKTX
        KTX_OMIT_MASTER
        KTX_FEATURE_WRITE=0
    )
    set_target_properties(ktx_read PROPERTIES FOLDER "hina-vk/KTX")
endif()

# Tracy Profiler (optional)
if(HINA_VK_ENABLE_TRACY)
    if(NOT TARGET TracyClient)
        if(HINA_HAS_THIRD_PARTY)
            set(TRACY_ENABLE ON CACHE BOOL "" FORCE)
            set(TRACY_ON_DEMAND ON CACHE BOOL "" FORCE)
            add_subdirectory(third_party/tracy)
            set_target_properties(TracyClient PROPERTIES FOLDER "hina-vk/Tracy")
        endif()

        # Error if Tracy enabled but not available
        if(NOT TARGET TracyClient)
            message(FATAL_ERROR "HINA_VK_ENABLE_TRACY is ON but TracyClient target not found. "
                "Either provide Tracy in your project or set HINA_VK_ENABLE_TRACY=OFF.")
        endif()
    endif()
endif()

# Dear ImGui (for examples)
if(NOT TARGET imgui)
    set(IMGUI_DIR ${CMAKE_CURRENT_SOURCE_DIR}/third_party/imgui)
    if(EXISTS ${IMGUI_DIR}/imgui.cpp)
        add_library(imgui STATIC
            ${IMGUI_DIR}/imgui.cpp
            ${IMGUI_DIR}/imgui_demo.cpp
            ${IMGUI_DIR}/imgui_draw.cpp
            ${IMGUI_DIR}/imgui_tables.cpp
            ${IMGUI_DIR}/imgui_widgets.cpp
        )
        target_include_directories(imgui PUBLIC ${IMGUI_DIR})
        set_target_properties(imgui PROPERTIES FOLDER "hina-vk/ImGui")
    endif()
endif()

# ---------------------------------------------------------------------------
# Library target
# ---------------------------------------------------------------------------

set(HINA_VK_SOURCES
    src/hina_vk.c
    src/hina_vk_impl.cpp
    include/hina_vk.h
)

# SPIRV-Reflect source (bundled or user-provided)
if(HINA_HAS_THIRD_PARTY)
    list(APPEND HINA_VK_SOURCES ${SPIRV_Reflect_SOURCE_DIR}/spirv_reflect.c)
else()
    # In embedded mode, user must provide spirv_reflect.c in src/
    if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/src/spirv_reflect.c")
        list(APPEND HINA_VK_SOURCES src/spirv_reflect.c)
    else()
        message(FATAL_ERROR "spirv_reflect.c not found. Copy it to src/ or use the full repo with third_party/.")
    endif()
endif()

add_library(hina-vk STATIC ${HINA_VK_SOURCES})

# Alias for consistent usage: hina::vk
add_library(hina::vk ALIAS hina-vk)

set_target_properties(hina-vk PROPERTIES
    FOLDER "hina-vk"
    VERSION ${PROJECT_VERSION}
    SOVERSION ${PROJECT_VERSION_MAJOR}
    C_VISIBILITY_PRESET hidden
    VISIBILITY_INLINES_HIDDEN ON
)

target_include_directories(hina-vk
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
)

# Private include directories (only when using bundled deps)
if(HINA_HAS_THIRD_PARTY)
    target_include_directories(hina-vk PRIVATE
        ${volk_SOURCE_DIR}
        ${Vulkan_Headers_SOURCE_DIR}/include
        ${VulkanMemoryAllocator_SOURCE_DIR}/include
        ${SPIRV_Reflect_SOURCE_DIR}
    )
endif()
# In embedded mode, parent project must ensure these headers are in the include path

# Shader compiler dependencies (only when enabled)
if(HINA_VK_ENABLE_SHADER_COMPILER)
    if(HINA_HAS_THIRD_PARTY)
        # Get glslang include path from bundled deps
        if(NOT DEFINED glslang_SOURCE_DIR)
            set(glslang_SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/third_party/glslang")
        endif()
        if(NOT DEFINED SPIRV_Tools_SOURCE_DIR)
            set(SPIRV_Tools_SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/third_party/SPIRV-Tools")
        endif()
        target_include_directories(hina-vk PRIVATE
            ${glslang_SOURCE_DIR}/glslang/Include
            ${glslang_SOURCE_DIR}
            ${SPIRV_Tools_SOURCE_DIR}/include
            ${SPIRV_Tools_SOURCE_DIR}
        )
    endif()
    # In embedded mode, parent project must ensure glslang headers are in the include path
    # Note: SPIRV-Tools-lint depends on SPIRV-Tools-opt which is pulled in transitively
    target_link_libraries(hina-vk PRIVATE glslang glslang-default-resource-limits SPIRV-Tools-lint)
else()
    # Define HINA_NO_SHADER_COMPILER to disable shader compilation in headers/source
    target_compile_definitions(hina-vk PUBLIC HINA_NO_SHADER_COMPILER)
endif()

# Tracy profiler (when enabled, propagates to examples via PUBLIC)
if(HINA_VK_ENABLE_TRACY)
    target_link_libraries(hina-vk PUBLIC TracyClient)
    target_compile_definitions(hina-vk PUBLIC TRACY_ENABLE)
endif()

set_target_properties(hina-vk PROPERTIES LINKER_LANGUAGE CXX)

# Enable LTO/IPO for Release builds when supported
include(CheckIPOSupported)
check_ipo_supported(RESULT HINA_IPO_SUPPORTED OUTPUT HINA_IPO_ERROR)
if (HINA_IPO_SUPPORTED)
    set_property(TARGET hina-vk PROPERTY INTERPROCEDURAL_OPTIMIZATION_RELEASE TRUE)
else()
    message(STATUS "LTO disabled: ${HINA_IPO_ERROR}")
endif()

target_compile_definitions(hina-vk PRIVATE VOLK_STATIC_DEFINES)

if (WIN32)
    target_compile_definitions(hina-vk PRIVATE VK_USE_PLATFORM_WIN32_KHR)
elseif (APPLE)
    target_compile_definitions(hina-vk PRIVATE VK_USE_PLATFORM_MACOS_MVK)
elseif (UNIX)
    target_compile_definitions(hina-vk PRIVATE VK_USE_PLATFORM_XLIB_KHR)
endif()


# ---------------------------------------------------------------------------
# Example Helper Function
# ---------------------------------------------------------------------------
# Helper function to set up an example with common dependencies including ImGui
function(hina_add_example TARGET_NAME)
    # Link against hina-vk and SDL2
    target_link_libraries(${TARGET_NAME} PRIVATE hina-vk SDL2-static)
    if(TARGET SDL2main)
        target_link_libraries(${TARGET_NAME} PRIVATE SDL2main)
    endif()

    # GLM include path and configuration
    target_include_directories(${TARGET_NAME} PRIVATE
        ${CMAKE_SOURCE_DIR}/third_party/glm
    )
    target_compile_definitions(${TARGET_NAME} PRIVATE
        GLM_FORCE_RADIANS
        GLM_FORCE_DEPTH_ZERO_TO_ONE
    )

    # Add ImGui support if available
    if(TARGET imgui)
        target_link_libraries(${TARGET_NAME} PRIVATE imgui)
        target_compile_definitions(${TARGET_NAME} PRIVATE HINA_EXAMPLE_HAS_IMGUI)
    endif()

    # Set folder for IDE organization
    set_target_properties(${TARGET_NAME} PROPERTIES FOLDER "Examples")
endfunction()

# ---------------------------------------------------------------------------
# Examples
# ---------------------------------------------------------------------------
if(HINA_VK_BUILD_EXAMPLES)
    add_subdirectory(examples)
endif()

# ---------------------------------------------------------------------------
# Configuration summary
# ---------------------------------------------------------------------------
message(STATUS "")
message(STATUS "hina-vk ${PROJECT_VERSION} configuration:")
message(STATUS "  Build type:        ${CMAKE_BUILD_TYPE}")
if(HINA_HAS_THIRD_PARTY)
    message(STATUS "  Dependencies:      bundled (third_party/)")
else()
    message(STATUS "  Dependencies:      embedded (from parent project)")
endif()
message(STATUS "  Shader compiler:   ${HINA_VK_ENABLE_SHADER_COMPILER}")
message(STATUS "  Tracy profiler:    ${HINA_VK_ENABLE_TRACY}")
message(STATUS "  Build examples:    ${HINA_VK_BUILD_EXAMPLES}")
message(STATUS "")

// Platform defines are set by CMake
extern "C" {
#define VOLK_IMPLEMENTATION
#include "volk.h"
}

#define VMA_IMPLEMENTATION
#define VMA_STATIC_VULKAN_FUNCTIONS 0
#include "vk_mem_alloc.h"

#ifdef TRACY_ENABLE
#include <tracy/TracyVulkan.hpp>
#include <new>

extern "C" {

void* hina_tracy_vk_init(VkPhysicalDevice phys, VkDevice dev, VkQueue queue, VkCommandBuffer cmd)
{
  return TracyVkContext(phys, dev, queue, cmd);
}

void hina_tracy_vk_shutdown(void* ctx)
{
  TracyVkDestroy(static_cast<TracyVkCtx>(ctx));
}

// cmdbuf can be VK_NULL_HANDLE for host-side query reset (VK 1.2+)
void hina_tracy_vk_collect(void* ctx, VkCommandBuffer cmdbuf)
{
  TracyVkCollect(static_cast<TracyVkCtx>(ctx), cmdbuf);
}

// Placement new into caller-provided storage; uses TracyVkZoneTransient-style runtime strings
void hina_tracy_vk_zone_begin(void* ctx, void* scope_storage, VkCommandBuffer cmdbuf,
                               uint32_t line, const char* source, const char* function, const char* name)
{
    new (scope_storage) tracy::VkCtxScope(
        static_cast<TracyVkCtx>(ctx),
        line,
        source, strlen(source),
        function, strlen(function),
        name, strlen(name),
        cmdbuf,
        true
    );
}

void hina_tracy_vk_zone_end(void* scope_storage)
{
    static_cast<tracy::VkCtxScope*>(scope_storage)->~VkCtxScope();
}

void hina_tracy_vk_context_name(void* ctx, const char* name)
{
    if (name) {
        TracyVkContextName(static_cast<TracyVkCtx>(ctx), name, static_cast<uint16_t>(strlen(name)));
    }
}

}
#endif

#ifndef HINA_NO_SHADER_COMPILER
#include <spirv-tools/linter.hpp>
#include <string>
extern "C" {

// Catches divergent derivatives (valid SPIR-V but undefined behavior)
static std::string g_lint_warnings;

bool hina_spirv_lint(const uint32_t* spirv_words, size_t word_count)
{
    g_lint_warnings.clear();
    spvtools::Linter linter(SPV_ENV_VULKAN_1_0);
    linter.SetMessageConsumer([](spv_message_level_t level, const char*, const spv_position_t&, const char* message) {
        if (!g_lint_warnings.empty()) g_lint_warnings += "\n";
        const char* lvl = "INFO";
        switch (level) {
            case SPV_MSG_FATAL: lvl = "FATAL"; break;
            case SPV_MSG_INTERNAL_ERROR: lvl = "INTERNAL"; break;
            case SPV_MSG_ERROR: lvl = "ERROR"; break;
            case SPV_MSG_WARNING: lvl = "WARNING"; break;
            case SPV_MSG_INFO: lvl = "INFO"; break;
            case SPV_MSG_DEBUG: lvl = "DEBUG"; break;
        }
        g_lint_warnings += "[LINT ";
        g_lint_warnings += lvl;
        g_lint_warnings += "] ";
        g_lint_warnings += message;
    });
    return linter.Run(spirv_words, word_count);
}

// Returned pointer valid until next hina_spirv_lint call
const char* hina_spirv_lint_get_warnings(void)
{
    return g_lint_warnings.empty() ? nullptr : g_lint_warnings.c_str();
}

// Release thread-local string memory (call during shutdown)
void hina_spirv_lint_cleanup(void)
{
    g_lint_warnings.clear();
    g_lint_warnings.shrink_to_fit();
}

}
#endif

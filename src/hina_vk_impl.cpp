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

#if !defined(HINA_NO_SPIRV_LINT)

#include <spirv-tools/linter.hpp>
#include <cstddef>
#include <cstdlib>
#include <cstring>

extern "C" {

// Catches divergent derivatives (valid SPIR-V but undefined behavior)
//
// NOTE:
// Keep this TLS state as POD (no std::string / dynamic TLS constructors).
// On MSVC, dynamic TLS object lifetime can outlive _CrtDumpMemoryLeaks timing,
// producing false-positive leak noise in debug builds.
typedef struct hina_lint_tls_state
{
    char* data;
    size_t len;
    size_t cap;
} hina_lint_tls_state;

static thread_local hina_lint_tls_state g_lint_warnings = {nullptr, 0u, 0u};

static bool hina_lint_reserve(size_t needed)
{
    if (needed <= g_lint_warnings.cap) return true;

    size_t new_cap = g_lint_warnings.cap ? g_lint_warnings.cap : 256u;
    while (new_cap < needed)
    {
        if (new_cap > (needed / 2u))
        {
            new_cap = needed;
            break;
        }
        new_cap *= 2u;
    }

    char* new_data = static_cast<char*>(realloc(g_lint_warnings.data, new_cap));
    if (!new_data) return false;

    g_lint_warnings.data = new_data;
    g_lint_warnings.cap = new_cap;
    return true;
}

static void hina_lint_clear(void)
{
    g_lint_warnings.len = 0u;
    if (g_lint_warnings.data) g_lint_warnings.data[0] = '\0';
}

static void hina_lint_append(const char* text)
{
    if (!text || text[0] == '\0') return;

    const size_t text_len = strlen(text);
    const size_t needed = g_lint_warnings.len + text_len + 1u;
    if (!hina_lint_reserve(needed)) return;

    memcpy(g_lint_warnings.data + g_lint_warnings.len, text, text_len + 1u);
    g_lint_warnings.len += text_len;
}

static void hina_lint_release(void)
{
    free(g_lint_warnings.data);
    g_lint_warnings.data = nullptr;
    g_lint_warnings.len = 0u;
    g_lint_warnings.cap = 0u;
}

bool hina_spirv_lint(const uint32_t* spirv_words, size_t word_count)
{
    hina_lint_clear();
    spvtools::Linter linter(SPV_ENV_VULKAN_1_0);
    linter.SetMessageConsumer([](spv_message_level_t level, const char*, const spv_position_t&, const char* message) {
        if (g_lint_warnings.len != 0u) hina_lint_append("\n");
        const char* lvl = "INFO";
        switch (level) {
            case SPV_MSG_FATAL: lvl = "FATAL"; break;
            case SPV_MSG_INTERNAL_ERROR: lvl = "INTERNAL"; break;
            case SPV_MSG_ERROR: lvl = "ERROR"; break;
            case SPV_MSG_WARNING: lvl = "WARNING"; break;
            case SPV_MSG_INFO: lvl = "INFO"; break;
            case SPV_MSG_DEBUG: lvl = "DEBUG"; break;
        }
        hina_lint_append("[LINT ");
        hina_lint_append(lvl);
        hina_lint_append("] ");
        hina_lint_append(message ? message : "(null)");
    });
    return linter.Run(spirv_words, word_count);
}

// Returned pointer valid until next hina_spirv_lint call
const char* hina_spirv_lint_get_warnings(void)
{
    return (g_lint_warnings.data && g_lint_warnings.len != 0u) ? g_lint_warnings.data : nullptr;
}

// Fully release the calling thread's lint buffer allocation.
// Call this in deterministic shutdown paths to keep debug leak reports clean.
void hina_spirv_lint_cleanup(void)
{
    hina_lint_release();
}

}

#else

extern "C" {

bool hina_spirv_lint(const uint32_t*, size_t)
{
    return true;
}

const char* hina_spirv_lint_get_warnings(void)
{
    return nullptr;
}

void hina_spirv_lint_cleanup(void)
{
}

}

#endif // !HINA_NO_SPIRV_LINT

#endif // !HINA_NO_SHADER_COMPILER

# Android Shared Libraries

This directory contains shared native libraries used by all Android example apps.

## Vulkan Validation Layer

`jniLibs/arm64-v8a/libVkLayer_khronos_validation.so`

The Khronos Vulkan validation layer for Android arm64. This enables runtime validation
of Vulkan API usage, which is essential for debugging.

### Updating the Validation Layer

Download the latest Android binaries from:
https://github.com/KhronosGroup/Vulkan-ValidationLayers/releases

Extract `libVkLayer_khronos_validation.so` for the arm64-v8a architecture and replace
the file in this directory.

Alternatively, copy from your Android NDK installation:
```
$ANDROID_NDK/sources/third_party/vulkan/src/build-android/jniLibs/arm64-v8a/
```

### Usage

The validation layer is automatically included in debug builds via the shared jniLibs
configuration in the root `build.gradle`. To enable it at runtime, the app must:

1. Load the layer: `vkEnumerateInstanceLayerProperties()` should list it
2. Enable it: Pass `"VK_LAYER_KHRONOS_validation"` in `VkInstanceCreateInfo.ppEnabledLayerNames`

HinaVK automatically enables validation layers in debug builds when available.

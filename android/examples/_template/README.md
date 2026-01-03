# HinaVK Android Example Template

This is a template for creating new Android examples. Follow these steps to port a desktop example to Android:

## Quick Start

1. **Copy this template:**
   ```bash
   cp -r android/examples/_template android/examples/YOUR_EXAMPLE
   ```

2. **Rename the package:**
   - Edit `build.gradle`: Change `namespace` and `applicationId` to `com.hinavk.YOUR_EXAMPLE`
   - Rename Java folder: `src/main/java/com/hinavk/example/` → `src/main/java/com/hinavk/YOUR_EXAMPLE/`
   - Edit `VulkanActivity.java`: Change package to `com.hinavk.YOUR_EXAMPLE`
   - Edit `AndroidManifest.xml`: Change activity class to `com.hinavk.YOUR_EXAMPLE.VulkanActivity`

3. **Update CMakeLists.txt:**
   - Change `project(example ...)` to `project(YOUR_EXAMPLE ...)`
   - Update the source file path to point to your example's `main.cpp`

4. **Copy shaders:**
   - Copy your `.hina_sl` shader files to `src/main/assets/shaders/`
   - Or update `build.gradle` to copy from your example directory

5. **Build:**
   ```bash
   cd android
   ./gradlew :YOUR_EXAMPLE:assembleDebug
   ```

6. **Install:**
   ```bash
   adb install examples/YOUR_EXAMPLE/build/outputs/apk/debug/YOUR_EXAMPLE-debug.apk
   ```

## File Structure

```
_template/
├── build.gradle              # Gradle build config
├── CMakeLists.txt           # Native build config
├── src/main/
│   ├── AndroidManifest.xml  # App manifest
│   ├── java/com/hinavk/example/
│   │   └── VulkanActivity.java  # NativeActivity wrapper
│   ├── assets/shaders/      # Shader files go here
│   └── res/mipmap-*/        # App icons
└── README.md                # This file
```

## Notes

- Your example's `main.cpp` must include `hina_example.h` and use the cross-platform API
- The `android_main` entry point is defined by your `main.cpp` with `#ifdef __ANDROID__`
- Shaders are loaded from assets via `hina_example_load_file(app, "shaders/your_shader.hina_sl")`

#!/usr/bin/env python3
"""
create_android_example.py - Generate Android project from desktop example

Usage:
    python scripts/create_android_example.py <example_name>
    python scripts/create_android_example.py --all  # Convert all examples

Examples:
    python scripts/create_android_example.py triangle
    python scripts/create_android_example.py descriptorsets
    python scripts/create_android_example.py --all
"""

import os
import sys
import shutil
import glob
import argparse
from pathlib import Path

# Get repo root (script is in scripts/)
SCRIPT_DIR = Path(__file__).parent.resolve()
REPO_ROOT = SCRIPT_DIR.parent
EXAMPLES_DIR = REPO_ROOT / "examples"
ANDROID_DIR = REPO_ROOT / "android"
ANDROID_EXAMPLES_DIR = ANDROID_DIR / "examples"

# Examples to skip (don't have proper main.cpp structure or are deprecated)
SKIP_EXAMPLES = set()  # All deprecated examples have been removed

# Template for build.gradle
BUILD_GRADLE_TEMPLATE = '''apply plugin: 'com.android.application'

android {{
    namespace "com.hinavk.{example_id}"
    compileSdk rootProject.ext.compileSdkVersion
    ndkVersion rootProject.ext.ndkVersion

    defaultConfig {{
        applicationId "com.hinavk.{example_id}"
        minSdk rootProject.ext.minSdkVersion
        targetSdk rootProject.ext.targetSdkVersion
        versionCode 1
        versionName "1.0"

        externalNativeBuild {{
            cmake {{
                arguments "-DANDROID_STL=c++_shared",
                          "-DANDROID_TOOLCHAIN=clang"
                cppFlags "-std=c++17"
            }}
        }}

        ndk {{
            abiFilters rootProject.ext.abiFilters as String[]
        }}
    }}

    buildTypes {{
        release {{
            minifyEnabled false
            proguardFiles getDefaultProguardFile('proguard-android-optimize.txt')
        }}
        debug {{
            debuggable true
            jniDebuggable true
        }}
    }}

    externalNativeBuild {{
        cmake {{
            path "CMakeLists.txt"
            version "3.22.1"
        }}
    }}

    sourceSets {{
        main {{
            java.srcDirs = ['src/main/java']
            assets.srcDirs = ['src/main/assets']
        }}
    }}

    compileOptions {{
        sourceCompatibility JavaVersion.VERSION_1_8
        targetCompatibility JavaVersion.VERSION_1_8
    }}
}}

// Copy shaders from example directory to assets
task copyShaders(type: Copy) {{
    from("${{rootProject.ext.examplesDir}}/{example_name}") {{
        include "*.hina_sl"
    }}
    into "src/main/assets/shaders"
}}

// Copy textures if they exist
task copyTextures(type: Copy) {{
    from("${{rootProject.ext.examplesDir}}/{example_name}/textures") {{
        include "**/*"
    }}
    into "src/main/assets/textures"
}}

preBuild.dependsOn copyShaders
preBuild.dependsOn copyTextures

dependencies {{
    implementation fileTree(dir: 'libs', include: ['*.jar'])
}}
'''

# CMakeLists.txt is copied from _template and modified

# Template for AndroidManifest.xml
MANIFEST_TEMPLATE = '''<?xml version="1.0" encoding="utf-8"?>
<manifest xmlns:android="http://schemas.android.com/apk/res/android">

    <!-- Vulkan hardware feature -->
    <uses-feature
        android:name="android.hardware.vulkan.level"
        android:version="1"
        android:required="true" />
    <uses-feature
        android:name="android.hardware.vulkan.version"
        android:version="0x00401000"
        android:required="true" />

    <!-- Optional features -->
    <uses-feature android:name="android.hardware.touchscreen" android:required="false" />
    <uses-feature android:name="android.hardware.gamepad" android:required="false" />

    <application
        android:label="{display_name}"
        android:icon="@mipmap/ic_launcher"
        android:allowBackup="true"
        android:theme="@android:style/Theme.NoTitleBar.Fullscreen"
        android:hardwareAccelerated="true">

        <activity
            android:name="com.hinavk.{example_id}.VulkanActivity"
            android:label="{display_name}"
            android:configChanges="orientation|keyboardHidden"
            android:screenOrientation="landscape"
            android:exported="true">
            <meta-data android:name="android.app.lib_name" android:value="main" />
            <intent-filter>
                <action android:name="android.intent.action.MAIN" />
                <category android:name="android.intent.category.LAUNCHER" />
            </intent-filter>
        </activity>
    </application>

</manifest>
'''

# Template for VulkanActivity.java
ACTIVITY_TEMPLATE = '''package com.hinavk.{example_id};

import android.app.AlertDialog;
import android.app.NativeActivity;
import android.content.DialogInterface;
import android.content.pm.ApplicationInfo;
import android.os.Bundle;

import java.util.concurrent.Semaphore;

public class VulkanActivity extends NativeActivity {{

    static {{
        System.loadLibrary("main");
    }}

    @Override
    protected void onCreate(Bundle savedInstanceState) {{
        super.onCreate(savedInstanceState);
    }}

    private final Semaphore semaphore = new Semaphore(0, true);

    public void showAlert(final String message) {{
        final VulkanActivity activity = this;
        ApplicationInfo applicationInfo = activity.getApplicationInfo();
        final String applicationName = applicationInfo.nonLocalizedLabel.toString();

        this.runOnUiThread(new Runnable() {{
            public void run() {{
                AlertDialog.Builder builder = new AlertDialog.Builder(activity, android.R.style.Theme_Material_Dialog_Alert);
                builder.setTitle(applicationName);
                builder.setMessage(message);
                builder.setPositiveButton("Close", new DialogInterface.OnClickListener() {{
                    public void onClick(DialogInterface dialog, int id) {{
                        semaphore.release();
                    }}
                }});
                builder.setCancelable(false);
                AlertDialog dialog = builder.create();
                dialog.show();
            }}
        }});
        try {{
            semaphore.acquire();
        }} catch (InterruptedException e) {{
            Thread.currentThread().interrupt();
        }}
    }}
}}
'''


def get_display_name(example_name: str) -> str:
    """Convert example_name to display name (e.g., descriptorsets -> Descriptor Sets)"""
    # Common abbreviations
    replacements = {
        "gltf": "glTF",
        "pbr": "PBR",
        "sdl": "SDL",
        "vk": "VK",
        "hdr": "HDR",
    }

    words = []
    current = ""
    for c in example_name:
        if c == '_':
            if current:
                words.append(current)
                current = ""
        else:
            current += c
    if current:
        words.append(current)

    result = []
    for word in words:
        lower = word.lower()
        if lower in replacements:
            result.append(replacements[lower])
        else:
            result.append(word.capitalize())

    return "HinaVK " + " ".join(result)


def get_example_id(example_name: str) -> str:
    """Convert example name to valid Java package identifier"""
    return example_name.replace("-", "_").replace(" ", "_").lower()


def check_example_has_android_support(example_path: Path) -> bool:
    """Check if example main.cpp has android_main entry point or uses hina_example.h"""
    main_cpp = example_path / "main.cpp"
    if not main_cpp.exists():
        return False

    content = main_cpp.read_text()
    # Check for android_main or __ANDROID__ preprocessor
    if "android_main" in content or "__ANDROID__" in content:
        return True
    # Check if it includes hina_example.h which provides cross-platform support
    # Examples using hina_example.h need android_main added during conversion
    if 'hina_example.h' in content:
        return True
    return False


def create_android_example(example_name: str, force: bool = False) -> bool:
    """Create Android project for the given example"""

    example_path = EXAMPLES_DIR / example_name
    if not example_path.exists():
        print(f"Error: Example '{example_name}' not found in {EXAMPLES_DIR}")
        return False

    if example_name in SKIP_EXAMPLES:
        print(f"Skipping '{example_name}' (in skip list)")
        return False

    if not check_example_has_android_support(example_path):
        print(f"Skipping '{example_name}' (no Android support in main.cpp)")
        return False

    android_example_path = ANDROID_EXAMPLES_DIR / example_name

    if android_example_path.exists():
        if force:
            print(f"Removing existing Android project: {android_example_path}")
            shutil.rmtree(android_example_path)
        else:
            print(f"Android project already exists: {android_example_path} (use --force to overwrite)")
            return False

    example_id = get_example_id(example_name)
    display_name = get_display_name(example_name)

    print(f"Creating Android project for '{example_name}'...")
    print(f"  Package: com.hinavk.{example_id}")
    print(f"  Display: {display_name}")

    # Create directory structure
    java_dir = android_example_path / "src/main/java/com/hinavk" / example_id
    res_dirs = [
        android_example_path / "src/main/res/mipmap-hdpi",
        android_example_path / "src/main/res/mipmap-mdpi",
        android_example_path / "src/main/res/mipmap-xhdpi",
        android_example_path / "src/main/res/mipmap-xxhdpi",
        android_example_path / "src/main/res/mipmap-anydpi-v26",
        android_example_path / "src/main/res/drawable",
    ]
    assets_dir = android_example_path / "src/main/assets/shaders"

    java_dir.mkdir(parents=True, exist_ok=True)
    assets_dir.mkdir(parents=True, exist_ok=True)
    for res_dir in res_dirs:
        res_dir.mkdir(parents=True, exist_ok=True)

    # Write build.gradle
    (android_example_path / "build.gradle").write_text(
        BUILD_GRADLE_TEMPLATE.format(example_name=example_name, example_id=example_id)
    )

    # Copy CMakeLists.txt from template and substitute example name
    template_cmake = ANDROID_EXAMPLES_DIR / "_template/CMakeLists.txt"
    if template_cmake.exists():
        cmake_content = template_cmake.read_text()
        # Replace template project name and source path
        cmake_content = cmake_content.replace("project(example ", f"project({example_name} ")
        cmake_content = cmake_content.replace("${HINA_EXAMPLES}/triangle/main.cpp", f"${{HINA_EXAMPLES}}/{example_name}/main.cpp")
        (android_example_path / "CMakeLists.txt").write_text(cmake_content)
    else:
        print(f"  Warning: Template CMakeLists.txt not found at {template_cmake}")
        return False

    # Write AndroidManifest.xml
    (android_example_path / "src/main/AndroidManifest.xml").write_text(
        MANIFEST_TEMPLATE.format(example_id=example_id, display_name=display_name)
    )

    # Write VulkanActivity.java
    (java_dir / "VulkanActivity.java").write_text(
        ACTIVITY_TEMPLATE.format(example_id=example_id)
    )

    # Copy icons from template or existing triangle example
    icon_source = ANDROID_EXAMPLES_DIR / "_template/src/main/res"
    if not icon_source.exists():
        icon_source = ANDROID_EXAMPLES_DIR / "triangle/src/main/res"

    if icon_source.exists():
        for res_dir in res_dirs:
            density = res_dir.name
            src_dir = icon_source / density
            if src_dir.exists():
                # Copy all files in the directory (PNG icons, XML files)
                for src_file in src_dir.iterdir():
                    if src_file.is_file():
                        shutil.copy(src_file, res_dir / src_file.name)

    print(f"  Created: {android_example_path}")
    return True


def get_all_examples() -> list:
    """Get list of all example directories"""
    examples = []
    for path in EXAMPLES_DIR.iterdir():
        if path.is_dir() and (path / "main.cpp").exists():
            examples.append(path.name)
    return sorted(examples)


def main():
    parser = argparse.ArgumentParser(
        description="Generate Android project from desktop example"
    )
    parser.add_argument(
        "example",
        nargs="?",
        help="Example name (e.g., triangle, descriptorsets)"
    )
    parser.add_argument(
        "--all",
        action="store_true",
        help="Convert all examples"
    )
    parser.add_argument(
        "--force",
        action="store_true",
        help="Overwrite existing Android projects"
    )
    parser.add_argument(
        "--list",
        action="store_true",
        help="List available examples"
    )

    args = parser.parse_args()

    if args.list:
        print("Available examples:")
        for name in get_all_examples():
            has_android = check_example_has_android_support(EXAMPLES_DIR / name)
            skip = name in SKIP_EXAMPLES
            status = ""
            if skip:
                status = " [skip]"
            elif not has_android:
                status = " [no Android support]"
            print(f"  {name}{status}")
        return 0

    if args.all:
        examples = get_all_examples()
        created = 0
        for name in examples:
            if create_android_example(name, args.force):
                created += 1
        print(f"\nCreated {created} Android projects")
        return 0

    if not args.example:
        parser.print_help()
        return 1

    if create_android_example(args.example, args.force):
        print("\nTo build:")
        print(f"  cd android && ./gradlew :{args.example}:assembleDebug")
        print("\nTo install:")
        print(f"  adb install android/examples/{args.example}/build/outputs/apk/debug/{args.example}-debug.apk")
        return 0

    return 1


if __name__ == "__main__":
    sys.exit(main())

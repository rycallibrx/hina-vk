# Shader Test

HSL (Hina Shading Language) compiler test harness.

**Purpose:**
Tests the HSL shader compiler with various edge cases including:
- Include file resolution from multiple search paths
- Error reporting with correct line numbers
- Syntax validation
- Shader module compilation and validation
- SPIR-V bytecode generation

This is a development/testing tool, not a visual example. It runs headless without creating a window.

**Usage:**
```bash
shader_test
```

The test will automatically locate test shader files relative to the executable path.

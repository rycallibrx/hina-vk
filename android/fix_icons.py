#!/usr/bin/env python3
"""
Generate adaptive icon foreground images.

Android adaptive icons:
- Total canvas: 108dp × 108dp
- Safe zone: 66dp × 66dp (center, always visible)
- Outer 18dp margin: may be cropped by device masks
"""

from PIL import Image
import os

DENSITY_SIZES = {
    'mdpi': 108,
    'hdpi': 162,
    'xhdpi': 216,
    'xxhdpi': 324,
}

def generate_foreground(source_path, output_path, size, scale=1.0):
    """
    Generate foreground icon.

    Args:
        source_path: Source image
        output_path: Output PNG path
        size: Target size in pixels
        scale: Scale factor (1.0 = source fills 66dp safe zone,
               larger = source fills more of canvas)
    """
    img = Image.open(source_path).convert('RGB')  # No alpha, avoid transparency issues

    # Make square by center-cropping
    w, h = img.size
    min_dim = min(w, h)
    left = (w - min_dim) // 2
    top = (h - min_dim) // 2
    img = img.crop((left, top, left + min_dim, top + min_dim))

    # Scale factor: 1.0 means source fits in 66dp safe zone (61.1% of canvas)
    # Scale 1.636 means source fills entire 108dp canvas
    # Default 1.0 shows full source in safe zone with bleed at edges

    base_ratio = 66.0 / 108.0  # 0.611 - safe zone ratio
    actual_ratio = base_ratio * scale

    if actual_ratio >= 1.0:
        # Source fills or exceeds canvas - just resize to fit
        img = img.resize((size, size), Image.Resampling.LANCZOS)
    else:
        # Source is smaller than canvas - center it
        inner_size = int(size * actual_ratio)
        img_small = img.resize((inner_size, inner_size), Image.Resampling.LANCZOS)

        # Create canvas with edge-sampled background color
        # Sample average color from edges of source
        pixels = img.resize((10, 10), Image.Resampling.LANCZOS).load()
        edge_colors = [pixels[0,i] for i in range(10)] + [pixels[9,i] for i in range(10)]
        avg_r = sum(c[0] for c in edge_colors) // len(edge_colors)
        avg_g = sum(c[1] for c in edge_colors) // len(edge_colors)
        avg_b = sum(c[2] for c in edge_colors) // len(edge_colors)

        img = Image.new('RGB', (size, size), (avg_r, avg_g, avg_b))
        offset = (size - inner_size) // 2
        img.paste(img_small, (offset, offset))

    # Convert to RGBA for PNG but ensure fully opaque
    img = img.convert('RGBA')

    os.makedirs(os.path.dirname(output_path), exist_ok=True)
    img.save(output_path, 'PNG')
    print(f"Generated {size}px (scale {scale}): {output_path}")


def main():
    import sys

    # Default: scale 1.636 fills entire canvas (like original behavior)
    scale = 1.636

    if len(sys.argv) > 1:
        arg = sys.argv[1]
        if arg == '--help':
            print(f"Usage: {sys.argv[0]} [scale]")
            print()
            print("Scale values:")
            print("  1.0   = Source fits in 66dp safe zone (shows full image, may have border)")
            print("  1.3   = Source fills ~80% of canvas")
            print("  1.636 = Source fills entire 108dp canvas (default, may crop edges)")
            print()
            print("Tip: Use 1.636 or higher to avoid any background border showing")
            return
        try:
            scale = float(arg)
        except ValueError:
            print(f"Invalid scale: {arg}")
            return

    script_dir = os.path.dirname(os.path.abspath(__file__))
    repo_root = os.path.dirname(script_dir)
    source_path = os.path.join(repo_root, 'images', 'hina-vk.jpg')

    if not os.path.exists(source_path):
        print(f"Source not found: {source_path}")
        return

    print(f"Source: {source_path}")
    print(f"Scale: {scale}")

    examples_dir = os.path.join(script_dir, 'examples')
    for name in os.listdir(examples_dir):
        example_path = os.path.join(examples_dir, name)
        if os.path.isdir(example_path):
            print(f"\n=== {name} ===")
            res_dir = os.path.join(example_path, 'src', 'main', 'res')
            for density, size in DENSITY_SIZES.items():
                fg_path = os.path.join(res_dir, f'mipmap-{density}', 'ic_launcher_foreground.png')
                generate_foreground(source_path, fg_path, size, scale)

    print("\nDone!")


if __name__ == '__main__':
    main()

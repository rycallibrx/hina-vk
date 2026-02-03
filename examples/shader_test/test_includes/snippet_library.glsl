#pragma once

snippet lighting {
    vec3 calc_diffuse(vec3 normal, vec3 light_dir, vec3 color) {
        return max(dot(normal, light_dir), 0.0) * color;
    }
}

#pragma once

group SharedScene = 0;

struct SharedData {
    mat4 mvp;
    vec4 color;
};

bindings(SharedScene, start=0) {
    uniform(std140) UBO {
        SharedData data;
    } ubo;
}

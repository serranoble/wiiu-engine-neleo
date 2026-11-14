#version 330 core

layout(location = 0) in vec3 aPos;
layout(location = 1) in vec3 aNormal;
layout(location = 2) in vec2 aTexCoord;
layout(location = 3) in vec4 aColor;

uniform mat4 uVP; // Matriz Vista-Proyección sin traslación

out vec2 vTexCoord;

void main() {
    vTexCoord = aTexCoord;
    vec4 pos = uVP * vec4(aPos, 1.0);
    // Z = W asegura profundidad máxima (1.0 en NDC tras el perspective divide)
    gl_Position = pos.xyww;
}

#version 330 core

layout(location = 0) in vec3 aPos;
layout(location = 1) in vec3 aNormal;
layout(location = 2) in vec2 aTexCoord;
layout(location = 3) in vec4 aColor;

// Per-instance 4x4 column-major model matrix stream (buffer 1, GX2_ATTRIB_INDEX_PER_INSTANCE, divisor = 1)
layout(location = 4) in vec4 aInstCol0;
layout(location = 5) in vec4 aInstCol1;
layout(location = 6) in vec4 aInstCol2;
layout(location = 7) in vec4 aInstCol3;

uniform mat4 uViewProj;

out vec3 vWorldPos;
out vec3 vNormal;
out vec2 vTexCoord;
out vec4 vColor;

void main() {
    mat4 instModel = mat4(aInstCol0, aInstCol1, aInstCol2, aInstCol3);
    vec4 worldPos = instModel * vec4(aPos, 1.0);
    vWorldPos = worldPos.xyz;
    vNormal = mat3(instModel) * aNormal;
    vTexCoord = aTexCoord;
    vColor = aColor;
    gl_Position = uViewProj * worldPos;
}

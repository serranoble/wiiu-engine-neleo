#version 330 core

layout(location = 0) in vec3 aPos;
layout(location = 1) in vec3 aNormal;
layout(location = 2) in vec2 aTexCoord;
layout(location = 3) in vec4 aColor;

// Empaquetado contiguo de 3 mat4 (48 floats) para garantizar offsets exactos 0, 16, 32 en CafeGLSL:
// [0] = uMVP, [1] = uModel, [2] = uLightVP
uniform mat4 uVsMats[3];

out vec3 vWorldPos;
out vec3 vNormal;
out vec2 vTexCoord;
out vec4 vColor;
out vec4 vLightSpacePos;
out vec3 vLocalPos;

void main() {
    vec4 worldPos = uVsMats[1] * vec4(aPos, 1.0);
    vWorldPos = worldPos.xyz;
    vNormal = mat3(uVsMats[1]) * aNormal;
    vTexCoord = aTexCoord;
    vColor = aColor;
    vLightSpacePos = uVsMats[2] * worldPos;
    vLocalPos = aPos;
    gl_Position = uVsMats[0] * vec4(aPos, 1.0);
}

#version 330 core

layout(location = 0) in vec3 aPos;
layout(location = 1) in vec3 aNormal;
layout(location = 2) in vec2 aTexCoord;
layout(location = 3) in vec4 aColor;
layout(location = 4) in uvec4 aBoneIndices;
layout(location = 5) in vec4 aBoneWeights;

uniform mat4 uMVP;
uniform mat4 uModel;
uniform mat4 uBones[24];

out vec3 vWorldPos;
out vec3 vNormal;
out vec2 vTexCoord;
out vec4 vColor;

void main() {
    // Linear Blend Skinning (LBS) en silicio AMD Latte GX2
    mat4 skinMat = uBones[aBoneIndices.x] * aBoneWeights.x +
                   uBones[aBoneIndices.y] * aBoneWeights.y +
                   uBones[aBoneIndices.z] * aBoneWeights.z +
                   uBones[aBoneIndices.w] * aBoneWeights.w;

    vec4 skinnedLocalPos = skinMat * vec4(aPos, 1.0);
    vec4 worldPos = uModel * skinnedLocalPos;
    gl_Position = uMVP * skinnedLocalPos;

    vWorldPos = worldPos.xyz;
    vNormal = normalize((uModel * skinMat * vec4(aNormal, 0.0)).xyz);
    vTexCoord = aTexCoord;
    vColor = aColor;
}

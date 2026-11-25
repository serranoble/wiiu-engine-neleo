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
    // Linear Blend Skinning (LBS) en silicio AMD Latte GX2 (acotado a 24 huesos)
    uvec4 bi = min(aBoneIndices, uvec4(23u));
    mat4 skinMat = uBones[bi.x] * aBoneWeights.x +
                   uBones[bi.y] * aBoneWeights.y +
                   uBones[bi.z] * aBoneWeights.z +
                   uBones[bi.w] * aBoneWeights.w;

    vec4 skinnedLocalPos = skinMat * vec4(aPos, 1.0);
    vec4 worldPos = uModel * skinnedLocalPos;
    gl_Position = uMVP * skinnedLocalPos;

    vWorldPos = worldPos.xyz;
    vec3 nRaw = (uModel * skinMat * vec4(aNormal, 0.0)).xyz;
    float nLenSq = dot(nRaw, nRaw);
    vNormal = mix(vec3(0.0, 1.0, 0.0), nRaw * inversesqrt(max(nLenSq, 1e-8)), step(1e-8, nLenSq));
    vTexCoord = aTexCoord;
    vColor = aColor;
}

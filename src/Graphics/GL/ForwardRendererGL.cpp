#include "Graphics/ForwardRendererGX2.hpp"
#include "Graphics/ShadowMapRendererGX2.hpp"
#include "Graphics/GL/GLFunctions.hpp"
#include "Graphics/Texture/TextureLoaderGX2.hpp"
#include "Graphics/Mesh/MeshBinaryFormat.hpp"
#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstring>

namespace Neleo {

namespace {

using namespace Graphics::GL;

struct GLRendererState {
    GLuint meshProgram{0u};
    GLuint instProgram{0u};
    GLuint skelProgram{0u};
    GLuint skyProgram{0u};
    GLuint depthProgram{0u};

    GLuint staticVao{0u};
    GLuint instVao{0u};
    GLuint skinnedVao{0u};
    GLuint skyVao{0u};
    GLuint depthVao{0u};
    GLuint instanceVbo{0u};
    GLuint whiteFallbackTex{0u};

    // Uniform locations: MeshProgram
    GLint mModel{-1}, mViewProj{-1}, mLightVP{-1}, mCamPos{-1}, mAlbedoTint{-1};
    GLint mHasDiffuse{-1}, mDiffuseTex{-1}, mHasEnv{-1}, mEnvTex{-1}, mShadowTex{-1};
    GLint mUvTiling{-1}, mAlphaCutout{-1}, mCustomParam{-1}, mShaderTime{-1};
    GLint mMatParams{-1}, mShadowParams{-1};
    GLint mRoughness{-1}, mMetallic{-1}, mEmissive{-1}, mShadingMode{-1};
    GLint mDirLightDir{-1}, mDirLightColor{-1};
    GLint mPointCount{-1}, mPointPosRad{-1}, mPointColInt{-1};
    GLint mSpotPosRange{-1}, mSpotDirInner{-1}, mSpotColOuter{-1}, mSpotInt{-1};

    // Uniform locations: InstProgram
    GLint iViewProj{-1}, iLightVP{-1}, iCamPos{-1}, iAlbedoTint{-1};
    GLint iHasDiffuse{-1}, iDiffuseTex{-1}, iHasEnv{-1}, iEnvTex{-1}, iShadowTex{-1};
    GLint iUvTiling{-1}, iAlphaCutout{-1}, iCustomParam{-1}, iShaderTime{-1};
    GLint iMatParams{-1}, iShadowParams{-1};
    GLint iRoughness{-1}, iMetallic{-1}, iEmissive{-1}, iShadingMode{-1};
    GLint iDirLightDir{-1}, iDirLightColor{-1};
    GLint iPointCount{-1}, iPointPosRad{-1}, iPointColInt{-1};
    GLint iSpotPosRange{-1}, iSpotDirInner{-1}, iSpotColOuter{-1}, iSpotInt{-1};

    // Uniform locations: SkelProgram
    GLint sModel{-1}, sViewProj{-1}, sCamPos{-1}, sBones{-1}, sBoneCount{-1};
    GLint sHasDiffuse{-1}, sDiffuseTex{-1}, sClayMode{-1}, sReflection{-1};
    GLint sDirLightDir{-1}, sDirLightColor{-1};

    // Uniform locations: SkyProgram
    GLint kSkyVP{-1}, kSkyTex{-1}, kIsCubeAtlas{-1};

    // Uniform location: DepthProgram
    GLint dMVP{-1};

    bool inDepthPrePass{false};
};

GLRendererState g_state{};

GLuint CompileShader(GLenum type, const char* source) noexcept {
    const auto& gl = GetFunctions();
    if (!gl.CreateShader || !gl.ShaderSource || !gl.CompileShader || !gl.GetShaderiv) {
        return 0u;
    }
    const GLuint sh = gl.CreateShader(type);
    if (sh == 0u) return 0u;
    gl.ShaderSource(sh, 1, &source, nullptr);
    gl.CompileShader(sh);
    GLint ok = 0;
    gl.GetShaderiv(sh, GL_COMPILE_STATUS_VAL, &ok);
    if (ok == 0) {
        if (gl.DeleteShader) gl.DeleteShader(sh);
        return 0u;
    }
    return sh;
}

GLuint LinkProgram(const char* vsSrc, const char* fsSrc) noexcept {
    const auto& gl = GetFunctions();
    if (!gl.CreateProgram || !gl.AttachShader || !gl.LinkProgram || !gl.GetProgramiv) {
        return 0u;
    }
    const GLuint vs = CompileShader(GL_VERTEX_SHADER_VAL, vsSrc);
    const GLuint fs = CompileShader(GL_FRAGMENT_SHADER_VAL, fsSrc);
    if (vs == 0u || fs == 0u) {
        if (gl.DeleteShader) {
            if (vs != 0u) gl.DeleteShader(vs);
            if (fs != 0u) gl.DeleteShader(fs);
        }
        return 0u;
    }
    const GLuint prog = gl.CreateProgram();
    if (prog == 0u) {
        if (gl.DeleteShader) { gl.DeleteShader(vs); gl.DeleteShader(fs); }
        return 0u;
    }
    gl.AttachShader(prog, vs);
    gl.AttachShader(prog, fs);
    gl.LinkProgram(prog);

    GLint linked = 0;
    gl.GetProgramiv(prog, GL_LINK_STATUS_VAL, &linked);
    if (gl.DeleteShader) {
        gl.DeleteShader(vs);
        gl.DeleteShader(fs);
    }
    if (linked == 0) {
        if (gl.DeleteProgram) gl.DeleteProgram(prog);
        return 0u;
    }
    return prog;
}

constexpr const char* kMeshVS = R"(
#version 330 core
layout(location = 0) in vec3 aPos;
layout(location = 1) in vec3 aNormal;
layout(location = 2) in vec2 aTexCoord;
layout(location = 3) in vec4 aColor;

uniform mat4 uModel;
uniform mat4 uViewProj;
uniform mat4 uLightVP;

out vec3 vWorldPos;
out vec3 vWorldNormal;
out vec2 vTexCoord;
out vec4 vColor;
out vec4 vLightSpacePos;
out vec3 vLocalPos;

void main() {
    vec4 worldPos = uModel * vec4(aPos, 1.0);
    mat3 m3 = mat3(uModel);
    float det = determinant(m3);
    mat3 nMat = (abs(det) > 1e-6) ? transpose(inverse(m3)) : m3;
    vec3 nWorld = nMat * aNormal;
    vWorldPos = worldPos.xyz;
    vWorldNormal = (length(nWorld) > 1e-5) ? normalize(nWorld) : vec3(0.0, 1.0, 0.0);
    vTexCoord = aTexCoord;
    vColor = aColor;
    vLightSpacePos = uLightVP * worldPos;
    vLocalPos = aPos;
    gl_Position = uViewProj * worldPos;
}
)";

constexpr const char* kInstVS = R"(
#version 330 core
layout(location = 0) in vec3 aPos;
layout(location = 1) in vec3 aNormal;
layout(location = 2) in vec2 aTexCoord;
layout(location = 3) in vec4 aColor;
layout(location = 4) in vec4 aInstCol0;
layout(location = 5) in vec4 aInstCol1;
layout(location = 6) in vec4 aInstCol2;
layout(location = 7) in vec4 aInstCol3;

uniform mat4 uViewProj;
uniform mat4 uLightVP;

out vec3 vWorldPos;
out vec3 vWorldNormal;
out vec2 vTexCoord;
out vec4 vColor;
out vec4 vLightSpacePos;
out vec3 vLocalPos;

void main() {
    mat4 instModel = mat4(aInstCol0, aInstCol1, aInstCol2, aInstCol3);
    vec4 worldPos = instModel * vec4(aPos, 1.0);
    mat3 m3 = mat3(instModel);
    float det = determinant(m3);
    mat3 nMat = (abs(det) > 1e-6) ? transpose(inverse(m3)) : m3;
    vec3 nWorld = nMat * aNormal;
    vWorldPos = worldPos.xyz;
    vWorldNormal = (length(nWorld) > 1e-5) ? normalize(nWorld) : vec3(0.0, 1.0, 0.0);
    vTexCoord = aTexCoord;
    vColor = aColor;
    vLightSpacePos = uLightVP * worldPos;
    vLocalPos = aPos;
    gl_Position = uViewProj * worldPos;
}
)";

constexpr const char* kMeshFS = R"(
#version 330 core
in vec3 vWorldPos;
in vec3 vWorldNormal;
in vec2 vTexCoord;
in vec4 vColor;
in vec4 vLightSpacePos;
in vec3 vLocalPos;

uniform vec3 uCamPos;
uniform vec4 uAlbedoTint;
uniform int uHasDiffuseTex;
uniform sampler2D uDiffuseTex;
uniform int uHasEnvTex;
uniform sampler2D uEnvTex;
uniform sampler2D uShadowMap;
uniform float uUvTiling;
uniform float uAlphaCutout;
uniform float uCustomParam;
uniform float uShaderTime;
uniform vec4 uMatParams;    // x=refl/alphaCutout/toon, y=norm/fog/outline, z=bump/fogDensity, w=ambient/alpha
uniform vec4 uShadowParams; // x=mode(0..4), y=depthBias, z=texelSize, w=minLit
uniform float uRoughness;
uniform float uMetallic;
uniform vec3 uEmissive;
uniform int uShadingMode;

uniform vec4 uDirLightDir;
uniform vec4 uDirLightColor;
uniform int uPointLightCount;
uniform vec4 uPointLightPosRadius[8];
uniform vec4 uPointLightColorIntensity[8];
uniform vec4 uSpotPosRange;
uniform vec4 uSpotDirInner;
uniform vec4 uSpotColorOuter;
uniform float uSpotIntensity;

out vec4 FragColor;

const float PI = 3.14159265359;

float Hash31(vec3 p) {
    p = fract(p * vec3(0.1031, 0.1030, 0.0973));
    p += dot(p, p.yxz + 33.33);
    return fract((p.x + p.y) * p.z);
}

float Noise3D(vec3 p) {
    vec3 i = floor(p);
    vec3 f = fract(p);
    vec3 u = f * f * (3.0 - 2.0 * f);

    float n000 = Hash31(i + vec3(0.0, 0.0, 0.0));
    float n100 = Hash31(i + vec3(1.0, 0.0, 0.0));
    float n010 = Hash31(i + vec3(0.0, 1.0, 0.0));
    float n110 = Hash31(i + vec3(1.0, 1.0, 0.0));
    float n001 = Hash31(i + vec3(0.0, 0.0, 1.0));
    float n101 = Hash31(i + vec3(1.0, 0.0, 1.0));
    float n011 = Hash31(i + vec3(0.0, 1.0, 1.0));
    float n111 = Hash31(i + vec3(1.0, 1.0, 1.0));

    float nx00 = mix(n000, n100, u.x);
    float nx10 = mix(n010, n110, u.x);
    float nx01 = mix(n001, n101, u.x);
    float nx11 = mix(n011, n111, u.x);
    return mix(mix(nx00, nx10, u.y), mix(nx01, nx11, u.y), u.z);
}

float Fbm3D(vec3 p) {
    float v = 0.54 * Noise3D(p);
    v += 0.30 * Noise3D(p * 2.03 + vec3(1.7, 9.2, 4.1));
    v += 0.16 * Noise3D(p * 4.07 + vec3(8.3, 2.8, 6.5));
    return v;
}

vec3 SampleSkyAtlas(vec3 d) {
    vec3 ad = abs(d);
    float face = 0.0;
    float u = 0.5;
    float v = 0.5;
    if (ad.x >= ad.y && ad.x >= ad.z) {
        float invM = 1.0 / max(ad.x, 0.00001);
        if (d.x >= 0.0) { face = 0.0; u = -d.z * invM; v = -d.y * invM; }
        else            { face = 1.0; u =  d.z * invM; v = -d.y * invM; }
    } else if (ad.y >= ad.x && ad.y >= ad.z) {
        float invM = 1.0 / max(ad.y, 0.00001);
        if (d.y >= 0.0) { face = 2.0; u =  d.x * invM; v =  d.z * invM; }
        else            { face = 3.0; u =  d.x * invM; v = -d.z * invM; }
    } else {
        float invM = 1.0 / max(ad.z, 0.00001);
        if (d.z >= 0.0) { face = 4.0; u =  d.x * invM; v = -d.y * invM; }
        else            { face = 5.0; u = -d.x * invM; v = -d.y * invM; }
    }
    u = clamp(u * 0.5 + 0.5, 0.004, 0.996);
    v = clamp(v * 0.5 + 0.5, 0.004, 0.996);
    float col = mod(face, 3.0);
    float row = floor(face / 3.0);
    return texture(uEnvTex, vec2((col + u) / 3.0, (row + v) / 2.0)).rgb;
}

float ComputeDirectionalShadow(vec4 lightSpacePos, vec3 N, vec3 L) {
    float shadowMode = uShadowParams.x;
    if (shadowMode < 0.5) return 1.0;

    vec3 projCoords = lightSpacePos.xyz / max(lightSpacePos.w, 0.0001);
    vec2 shadowUV = projCoords.xy * 0.5 + 0.5;
    float currentDepth = projCoords.z * 0.5 + 0.5;

    if (shadowUV.x <= 0.001 || shadowUV.x >= 0.999 ||
        shadowUV.y <= 0.001 || shadowUV.y >= 0.999 ||
        currentDepth >= 1.0 || currentDepth <= 0.0) {
        return 1.0;
    }

    float baseBias = uShadowParams.y;
    float NdotL = clamp(dot(N, L), 0.0, 1.0);
    float bias = (baseBias > 0.00001) ? max(baseBias * (1.0 - NdotL) * 2.5, baseBias) : 0.0;
    float texelSize = max(uShadowParams.z, 1.0 / 2048.0);
    float minLit = uShadowParams.w;

    float lit = 1.0;
    if (shadowMode < 1.5) {
        float closestDepth = texture(uShadowMap, shadowUV).r;
        lit = ((currentDepth - bias) <= closestDepth) ? 1.0 : 0.0;
    } else if (shadowMode < 2.5) {
        float sum = 0.0;
        for (int y = -1; y <= 1; ++y) {
            for (int x = -1; x <= 1; ++x) {
                float d = texture(uShadowMap, shadowUV + vec2(float(x), float(y)) * texelSize).r;
                sum += ((currentDepth - bias) <= d) ? 1.0 : 0.0;
            }
        }
        lit = sum / 9.0;
    } else {
        float sum = 0.0;
        for (int y = -2; y <= 2; ++y) {
            for (int x = -2; x <= 2; ++x) {
                float d = texture(uShadowMap, shadowUV + vec2(float(x), float(y)) * texelSize).r;
                sum += ((currentDepth - bias) <= d) ? 1.0 : 0.0;
            }
        }
        lit = sum / 25.0;
    }
    return mix(minLit, 1.0, lit);
}

float DistributionGGX(vec3 N, vec3 H, float roughness) {
    float a = roughness * roughness;
    float a2 = a * a;
    float NdotH = max(dot(N, H), 0.0);
    float denom = (NdotH * NdotH * (a2 - 1.0) + 1.0);
    return a2 / max(PI * denom * denom, 0.0001);
}

float GeometrySmith(float NdotV, float NdotL, float roughness) {
    float r = roughness + 1.0;
    float k = (r * r) * 0.125;
    float ggxV = NdotV / (NdotV * (1.0 - k) + k);
    float ggxL = NdotL / (NdotL * (1.0 - k) + k);
    return ggxV * ggxL;
}

vec3 EvalPBRLight(vec3 N, vec3 V, vec3 L, vec3 radiance, vec3 albedo, vec3 F0, float roughness, float metallic) {
    float NdotL = max(dot(N, L), 0.0);
    if (NdotL <= 0.0) return vec3(0.0);
    vec3 H = normalize(V + L);
    float NdotV = max(dot(N, V), 0.001);
    float D = DistributionGGX(N, H, roughness);
    float G = GeometrySmith(NdotV, NdotL, roughness);
    vec3 F = F0 + (vec3(1.0) - F0) * pow(clamp(1.0 - max(dot(H, V), 0.0), 0.0, 1.0), 5.0);
    vec3 spec = (D * G * F) / max(4.0 * NdotV * NdotL, 0.001);
    vec3 kD = (vec3(1.0) - F) * (1.0 - metallic);
    return (kD * albedo / PI + spec) * radiance * NdotL;
}

void main() {
    vec2 uv = vTexCoord * max(uUvTiling, 0.001);
    vec3 N = normalize(vWorldNormal);
    vec3 V = normalize(uCamPos - vWorldPos);
    vec3 Ldir = normalize(uDirLightDir.xyz);

    // Modo 9: Partículas Billboard Suaves (uCustomParam >= 0.5) o Toon / Cel Shading NPR con Outline
    if (uShadingMode >= 9) {
        if (uCustomParam >= 0.5) {
            vec2 c = vTexCoord * 2.0 - vec2(1.0);
            float r2 = dot(c, c);
            if (r2 > 1.0) discard;
            float mask = pow(clamp(1.0 - r2, 0.0, 1.0), 1.8);
            vec3 pCol = uAlbedoTint.rgb * (0.75 + 0.85 * mask);
            FragColor = vec4(clamp(pCol, 0.0, 1.0), clamp(mask * uMatParams.w, 0.0, 1.0));
            return;
        }
        float NdotV9 = clamp(dot(N, V), 0.0, 1.0);
        if (uMatParams.y > 0.5 && NdotV9 < 0.24) {
            FragColor = vec4(0.04, 0.05, 0.08, 1.0);
            return;
        }
        float NdotL9 = max(dot(N, Ldir), 0.0);
        float toonDiff = (floor(NdotL9 * 4.0) + 0.35) / 4.0;
        vec3 H9 = normalize(V + Ldir);
        float toonSpec = step(0.92, max(dot(N, H9), 0.0)) * 0.65;
        float rim = smoothstep(0.62, 0.92, 1.0 - NdotV9) * 0.32;
        vec3 vertRgb9 = (dot(vColor.rgb, vec3(1.0)) > 0.001) ? vColor.rgb : vec3(1.0);
        vec3 col9 = (vertRgb9 * uAlbedoTint.rgb) * (0.24 + toonDiff * 0.92) * uDirLightColor.rgb + vec3(toonSpec + rim);
        FragColor = vec4(clamp(col9, 0.0, 1.0), 1.0);
        return;
    }

    // Modo 8: Terreno Heightmap Splatting (sub=0), Mármol 3D (sub=1), Madera 3D (sub=2)
    if (uShadingMode == 8) {
        float subMode = uCustomParam;
        vec3 albedo8 = vec3(0.5);
        if (subMode < 0.5) {
            float h = clamp(vColor.r + (Noise3D(vWorldPos * 0.45) - 0.5) * 0.08, 0.0, 1.0);
            float slope = clamp(vColor.g, 0.0, 1.0);
            float detail = 0.86 + 0.28 * Noise3D(vWorldPos * 1.65);

            vec3 sandCol  = vec3(0.56, 0.46, 0.32) * detail;
            vec3 grassCol = vec3(0.20, 0.48, 0.19) * detail;
            vec3 rockCol  = vec3(0.42, 0.40, 0.39) * detail;
            vec3 snowCol  = vec3(0.92, 0.95, 0.99);

            vec3 ground = mix(sandCol, grassCol, smoothstep(0.08, 0.24, h));
            ground = mix(ground, rockCol, smoothstep(0.18, 0.42, slope));
            ground = mix(ground, snowCol, smoothstep(0.62, 0.80, h) * (1.0 - smoothstep(0.32, 0.58, slope)));
            albedo8 = ground;
        } else if (subMode < 1.5) {
            vec3 p3 = vLocalPos * 2.4;
            float turb = Fbm3D(p3);
            float vein = 0.5 + 0.5 * sin(p3.x * 3.8 + p3.y * 1.4 + turb * 6.2);
            vein = pow(clamp(vein, 0.0, 1.0), 0.55);
            albedo8 = mix(vec3(0.16, 0.20, 0.26), vec3(0.95, 0.94, 0.90), vein);
        } else {
            vec3 p3 = vLocalPos * 2.2;
            float turb = Fbm3D(p3 * 1.35);
            float ringDist = length(p3.xz * vec2(1.0, 1.3)) * 4.2 + turb * 0.95;
            float ring = 0.5 + 0.5 * sin(ringDist * 6.283185);
            albedo8 = mix(vec3(0.30, 0.16, 0.07), vec3(0.68, 0.44, 0.22), smoothstep(0.15, 0.85, ring));
        }

        float NdotV8 = clamp(dot(N, V), 0.0, 1.0);
        bool toonOn = (uMatParams.x > 0.5);
        if (toonOn && uMatParams.y > 0.5 && subMode >= 0.5 && NdotV8 < 0.22) {
            FragColor = vec4(0.04, 0.05, 0.08, 1.0);
            return;
        }

        float NdotL8 = max(dot(N, Ldir), 0.0);
        float diff8 = toonOn ? ((floor(NdotL8 * 4.0) + 0.35) / 4.0) : NdotL8;
        vec3 H8 = normalize(V + Ldir);
        float spec8 = toonOn ? (step(0.92, max(dot(N, H8), 0.0)) * 0.45)
                             : (pow(max(dot(N, H8), 0.0), 36.0) * 0.28);
        vec3 hemi8 = mix(vec3(0.12, 0.14, 0.18), vec3(0.30, 0.38, 0.50), N.y * 0.5 + 0.5);
        vec3 col8 = albedo8 * (hemi8 + uDirLightColor.rgb * (diff8 * 1.10)) + uDirLightColor.rgb * spec8;

        int ptCount8 = clamp(uPointLightCount, 0, 8);
        for (int i = 0; i < ptCount8; ++i) {
            vec3 toL = uPointLightPosRadius[i].xyz - vWorldPos;
            float d = length(toL);
            float rad = max(uPointLightPosRadius[i].w, 0.1);
            if (d < rad) {
                vec3 Lp = toL / max(d, 0.0001);
                float ff = clamp(1.0 - d / rad, 0.0, 1.0);
                float att = (ff * ff) / (1.0 + 0.20 * d * d);
                float ndl = max(dot(N, Lp), 0.0);
                col8 += albedo8 * uPointLightColorIntensity[i].rgb *
                        (uPointLightColorIntensity[i].w * att * (toonOn ? (floor(ndl * 3.0 + 0.5) / 3.0) : ndl));
            }
        }
        FragColor = vec4(clamp(col8, 0.0, 1.0), 1.0);
        return;
    }

    // Modo 7: Escena HDR Lineal (Emisivos > 1.0, Alpha-Cutout, Vitrales y Niebla)
    if (uShadingMode == 7) {
        vec4 texCol = (uHasDiffuseTex != 0) ? texture(uDiffuseTex, uv) : vec4(0.0);
        if (uMatParams.x > 0.5 && texCol.a < 0.5) {
            discard;
        }
        vec3 vertRgb7 = (dot(vColor.rgb, vec3(1.0)) > 0.001) ? vColor.rgb : vec3(1.0);
        vec3 baseCol = (uHasDiffuseTex != 0) ? (texCol.rgb * uAlbedoTint.rgb) : (vertRgb7 * uAlbedoTint.rgb);
        vec3 albedoLin = pow(clamp(baseCol, 0.0, 1.0), vec3(2.2));

        vec3 hdrCol;
        if (uCustomParam > 0.05) {
            float core = 0.65 + 0.35 * clamp(abs(dot(N, V)), 0.0, 1.0);
            hdrCol = albedoLin * (uCustomParam * core);
        } else {
            float NdotL = max(dot(N, Ldir), 0.0);
            vec3 H7 = normalize(V + Ldir);
            float r7 = clamp(uRoughness, 0.08, 1.0);
            float specPow = mix(96.0, 12.0, r7);
            float spec = pow(max(dot(N, H7), 0.0), specPow) * (1.0 - r7 * 0.7);
            vec3 hemi = mix(vec3(0.03, 0.04, 0.07), vec3(0.10, 0.13, 0.20), N.y * 0.5 + 0.5);
            hdrCol = albedoLin * (hemi + uDirLightColor.rgb * (NdotL * 1.15)) + uDirLightColor.rgb * (spec * 0.55);

            int ptCount7 = clamp(uPointLightCount, 0, 8);
            for (int i = 0; i < ptCount7; ++i) {
                vec3 toL = uPointLightPosRadius[i].xyz - vWorldPos;
                float d = length(toL);
                float rad = max(uPointLightPosRadius[i].w, 0.1);
                if (d < rad) {
                    vec3 Lp = toL / max(d, 0.0001);
                    float ff = clamp(1.0 - d / rad, 0.0, 1.0);
                    float att = (ff * ff) / (1.0 + 0.22 * d * d);
                    float ndl = max(dot(N, Lp), 0.0);
                    hdrCol += albedoLin * uPointLightColorIntensity[i].rgb * (uPointLightColorIntensity[i].w * att * ndl);
                }
            }
        }

        float fogMode = uMatParams.y;
        if (fogMode > 0.5) {
            float dist = length(uCamPos - vWorldPos);
            float density = max(uMatParams.z, 0.001);
            float fogFactor = 1.0;
            if (fogMode < 1.5) {
                fogFactor = clamp((38.0 - dist) / (38.0 - 6.0), 0.0, 1.0);
            } else if (fogMode < 2.5) {
                fogFactor = exp(-dist * density);
            } else {
                float f = dist * density;
                fogFactor = exp(-(f * f));
            }
            hdrCol = mix(vec3(0.035, 0.055, 0.095), hdrCol, clamp(fogFactor, 0.0, 1.0));
        }

        float alphaOut = clamp(uMatParams.w, 0.05, 1.0);
        FragColor = vec4(max(hdrCol, vec3(0.0)), alphaOut);
        return;
    }

    // Modo 6: Skybox CubeMap Atlas 3x2
    if (uShadingMode == 6) {
        vec3 skyCol = (uHasEnvTex != 0) ? SampleSkyAtlas(normalize(vLocalPos)) : vec3(0.45, 0.68, 0.90);
        FragColor = vec4(skyCol, 1.0);
        return;
    }

    // Modo 5: Agua Física Realista con Ondas + Reflexión Planar RTT + Refracción + Fresnel
    if (uShadingMode == 5) {
        float t = uShaderTime;
        float amp = max(uCustomParam, 0.05);
        vec2 p = vWorldPos.xz;
        float distCam = length(uCamPos - vWorldPos);
        float detailFade = clamp(1.0 - distCam / 60.0, 0.12, 1.0);

        float w1 = p.x * 0.48 + p.y * 0.28 + t * 1.45;
        float w2 = p.x * -0.36 + p.y * 0.62 + t * 1.15;
        float w3 = p.x * 0.85 - p.y * 0.72 + t * 1.95;

        float dhdx = (cos(w1) * 0.48 * 0.055 + cos(w2) * -0.36 * 0.048 + cos(w3) * 0.85 * 0.026) * amp;
        float dhdz = (cos(w1) * 0.28 * 0.055 + cos(w2) *  0.62 * 0.048 + cos(w3) * -0.72 * 0.026) * amp;
        float crest = sin(w1) * 0.45 + sin(w2) * 0.35 + sin(w3) * 0.20;

        float r1 = p.x * 2.35 + p.y * 1.75 + t * 2.90;
        float r2 = p.x * -2.85 + p.y * 2.40 - t * 2.45;
        float r3 = p.x * 4.60 - p.y * 3.85 + t * 3.70;
        float r4 = p.x * -5.10 - p.y * 4.30 - t * 3.25;

        float ripScale = amp * detailFade;
        dhdx += (cos(r1) * 2.35 * 0.015 + cos(r2) * -2.85 * 0.013 + cos(r3) * 4.60 * 0.007 + cos(r4) * -5.10 * 0.006) * ripScale;
        dhdz += (cos(r1) * 1.75 * 0.015 + cos(r2) *  2.40 * 0.013 + cos(r3) * -3.85 * 0.007 + cos(r4) * -4.30 * 0.006) * ripScale;

        vec3 Nw = normalize(vec3(-dhdx, 1.0, -dhdz));
        float NdotV = clamp(dot(Nw, V), 0.0, 1.0);

        vec3 R = reflect(-V, Nw);
        vec3 skyRefl = (uHasEnvTex != 0) ? SampleSkyAtlas(vec3(R.x, max(R.y, 0.01), R.z)) : vec3(0.46, 0.68, 0.88);
        vec3 reflCol = skyRefl;
        if (uMatParams.x > 0.5 && uHasDiffuseTex != 0) {
            vec2 reflNDC = vLightSpacePos.xy / max(vLightSpacePos.w, 0.0001);
            vec2 baseReflUV = reflNDC * 0.5 + 0.5;
            vec2 distUV = baseReflUV + Nw.xz * (0.068 * detailFade);
            vec2 clampedUV = clamp(distUV, 0.004, 0.996);
            vec3 planarCol = texture(uDiffuseTex, clampedUV).rgb;
            vec2 edgeDist = min(clampedUV, 1.0 - clampedUV);
            float edgeMask = smoothstep(0.004, 0.055, min(edgeDist.x, edgeDist.y));
            reflCol = mix(skyRefl, planarCol, edgeMask);
        }

        vec3 refrDir = refract(-V, Nw, 0.75);
        float radialDist = length(p - vec2(1.0, -1.8));
        float waterDepth = clamp(1.35 + radialDist * 0.14, 1.35, 5.20);
        float tBed = waterDepth / max(-refrDir.y, 0.18);
        vec2 bedXZ = p + refrDir.xz * tBed;

        float sandWave = 0.5 + 0.5 * sin(bedXZ.x * 0.9 + sin(bedXZ.y * 0.55) * 1.4);
        vec3 seabedCol = mix(vec3(0.42, 0.39, 0.30), vec3(0.62, 0.56, 0.42), sandWave);
        float cA = sin(bedXZ.x * 2.1 + bedXZ.y * 1.6 + t * 1.8) * cos(bedXZ.x * 1.5 - bedXZ.y * 2.3 - t * 1.4);
        float cB = sin(bedXZ.x * 3.1 - bedXZ.y * 2.4 - t * 2.2) * cos(bedXZ.x * 2.2 + bedXZ.y * 3.0 + t * 1.7);
        float caustics = pow(clamp(0.5 + 0.5 * (cA + cB), 0.0, 1.0), 4.0) * exp(-waterDepth * 0.32) * detailFade;
        seabedCol += uDirLightColor.rgb * (caustics * 0.72);

        vec3 transmittance = exp(-tBed * vec3(0.44, 0.14, 0.065));
        vec3 underwaterCol = mix(vec3(0.012, 0.075, 0.145), seabedCol, transmittance);

        float fresnel = 0.02 + 0.98 * pow(1.0 - NdotV, 5.0);
        vec3 col = mix(underwaterCol, reflCol, clamp(fresnel, 0.0, 1.0));

        float sss = pow(clamp(dot(V, -Ldir + Nw * 0.55), 0.0, 1.0), 3.0) * clamp(crest * 0.5 + 0.5, 0.0, 1.0);
        col += vec3(0.03, 0.34, 0.38) * (sss * 0.42 * detailFade);

        vec3 H = normalize(V + Ldir);
        float NdotH = max(dot(Nw, H), 0.0);
        float glitter = pow(NdotH, 384.0) * 3.4 * detailFade + pow(NdotH, 56.0) * 0.14;
        col += uDirLightColor.rgb * glitter;

        float horizonFade = smoothstep(32.0, 66.0, distCam);
        vec3 horizonSky = (uHasEnvTex != 0) ? SampleSkyAtlas(normalize(vec3(-V.x, max(-V.y, 0.015), -V.z)))
                                            : vec3(0.55, 0.72, 0.88);
        col = mix(col, horizonSky, horizonFade);
        FragColor = vec4(clamp(col, 0.0, 1.0), 1.0);
        return;
    }

    // Modo 4: Vidrio / Cristal Refractivo + Reflexión Fresnel
    if (uShadingMode == 4) {
        float ior = max(uCustomParam, 1.01);
        float ratio = 1.0 / ior;
        vec3 R = reflect(-V, N);
        vec3 T = refract(-V, N, ratio);
        if (dot(T, T) < 0.0001) T = R;
        float f0Scalar = (ior - 1.0) / (ior + 1.0);
        f0Scalar *= f0Scalar;
        float NdotV = clamp(dot(N, V), 0.0, 1.0);
        float fresnel = f0Scalar + (1.0 - f0Scalar) * pow(1.0 - NdotV, 5.0);
        vec3 reflCol = (uHasEnvTex != 0) ? SampleSkyAtlas(R) : vec3(0.55, 0.72, 0.90);
        vec3 refrCol = ((uHasEnvTex != 0) ? SampleSkyAtlas(T) : vec3(0.35, 0.55, 0.75)) * uAlbedoTint.rgb;
        vec3 col = mix(refrCol, reflCol, clamp(fresnel, 0.0, 1.0));
        vec3 H = normalize(V + Ldir);
        col += uDirLightColor.rgb * (pow(max(dot(N, H), 0.0), 128.0) * 1.2);
        FragColor = vec4(clamp(col, 0.0, 1.0), 1.0);
        return;
    }

    // Modos 0..3: PBR / Blinn-Phong / Debug + Shadow Map PCF
    vec4 texSample = (uHasDiffuseTex != 0) ? texture(uDiffuseTex, uv) : vec4(1.0);
    if (uAlphaCutout > 0.5 && texSample.a < 0.5) {
        discard;
    }

    float vColSum = vColor.r + vColor.g + vColor.b;
    vec3 vertRgb = (vColSum > 0.001) ? vColor.rgb : vec3(1.0);
    float vertAlpha = (vColor.a > 0.001) ? vColor.a : 1.0;
    vec3 albedo = texSample.rgb * vertRgb * uAlbedoTint.rgb;
    float alpha = texSample.a * vertAlpha * uAlbedoTint.a;

    if (uShadingMode == 2) {
        FragColor = vec4(N * 0.5 + 0.5, 1.0);
        return;
    }
    if (uShadingMode == 3) {
        FragColor = vec4(clamp(uRoughness, 0.0, 1.0), clamp(uMetallic, 0.0, 1.0), 0.15, 1.0);
        return;
    }

    float shadowFactor = ComputeDirectionalShadow(vLightSpacePos, N, Ldir);
    if (uShadowParams.x > 3.5) {
        vec3 dbgCol = mix(vec3(0.08, 0.12, 0.28), vec3(0.95, 0.96, 0.98), shadowFactor);
        FragColor = vec4(dbgCol, 1.0);
        return;
    }

    vec3 dirRad = uDirLightColor.rgb * (1.25 * shadowFactor);
    vec3 color = vec3(0.0);
    if (uShadingMode == 0) {
        float rough = clamp(uRoughness, 0.06, 1.0);
        float metal = clamp(uMetallic, 0.0, 1.0);
        vec3 F0 = mix(vec3(0.04), albedo, metal);
        color += EvalPBRLight(N, V, Ldir, dirRad, albedo, F0, rough, metal);

        int ptCount = clamp(uPointLightCount, 0, 8);
        for (int i = 0; i < ptCount; ++i) {
            vec3 toLight = uPointLightPosRadius[i].xyz - vWorldPos;
            float dist = length(toLight);
            float rad = max(uPointLightPosRadius[i].w, 0.001);
            if (dist < rad) {
                float att = clamp(1.0 - (dist / rad), 0.0, 1.0);
                att = att * att / (1.0 + dist * dist * 0.08);
                vec3 pRad = uPointLightColorIntensity[i].rgb * uPointLightColorIntensity[i].w * att;
                color += EvalPBRLight(N, V, toLight / max(dist, 0.0001), pRad, albedo, F0, rough, metal);
            }
        }

        if (uSpotIntensity > 0.001) {
            vec3 toSpot = uSpotPosRange.xyz - vWorldPos;
            float dist = length(toSpot);
            float range = max(uSpotPosRange.w, 0.001);
            if (dist < range) {
                vec3 sL = toSpot / max(dist, 0.0001);
                float theta = dot(sL, normalize(-uSpotDirInner.xyz));
                float coneT = clamp((theta - uSpotColorOuter.w) / max(uSpotDirInner.w - uSpotColorOuter.w, 0.0001), 0.0, 1.0);
                float cone = coneT * coneT * (3.0 - 2.0 * coneT);
                float att = clamp(1.0 - dist / range, 0.0, 1.0);
                vec3 sRad = uSpotColorOuter.rgb * uSpotIntensity * cone * att * att;
                color += EvalPBRLight(N, V, sL, sRad, albedo, F0, rough, metal);
            }
        }
        color += albedo * uMatParams.w;
    } else {
        float halfLam = dot(N, Ldir) * 0.5 + 0.5;
        vec3 H = normalize(V + Ldir);
        float spec = pow(max(dot(N, H), 0.0), max(uDirLightColor.w, 4.0)) * 0.35 * shadowFactor;
        color = albedo * (uMatParams.w + uDirLightColor.rgb * (halfLam * 0.85 * shadowFactor)) + uDirLightColor.rgb * spec;
    }

    if (uHasEnvTex != 0 && uMatParams.x > 0.001) {
        vec3 R = reflect(-V, N);
        vec3 envCol = SampleSkyAtlas(R);
        float fresnel = 0.08 + 0.92 * pow(1.0 - max(dot(N, V), 0.0), 4.0);
        color = mix(color, envCol, clamp(uMatParams.x * fresnel, 0.0, 0.9));
    }

    color += uEmissive;
    color = color / (color + vec3(1.0)) * 1.45;
    FragColor = vec4(clamp(color, 0.0, 1.0), alpha);
}
)";

constexpr const char* kSkinnedVS = R"(
#version 330 core
layout(location = 0) in vec3 aPos;
layout(location = 1) in vec3 aNormal;
layout(location = 2) in vec2 aTexCoord;
layout(location = 3) in vec4 aColor;
layout(location = 4) in uvec4 aBoneIndices;
layout(location = 5) in vec4 aBoneWeights;

uniform mat4 uModel;
uniform mat4 uViewProj;
uniform mat4 uBones[64];
uniform int uBoneCount;

out vec3 vWorldPos;
out vec3 vWorldNormal;
out vec2 vTexCoord;
out vec4 vColor;

void main() {
    mat4 skinMat = mat4(0.0);
    float wSum = aBoneWeights.x + aBoneWeights.y + aBoneWeights.z + aBoneWeights.w;
    if (uBoneCount > 0 && wSum > 0.0001) {
        float invW = 1.0 / wSum;
        int maxB = uBoneCount - 1;
        skinMat += uBones[clamp(int(aBoneIndices.x), 0, maxB)] * (aBoneWeights.x * invW);
        skinMat += uBones[clamp(int(aBoneIndices.y), 0, maxB)] * (aBoneWeights.y * invW);
        skinMat += uBones[clamp(int(aBoneIndices.z), 0, maxB)] * (aBoneWeights.z * invW);
        skinMat += uBones[clamp(int(aBoneIndices.w), 0, maxB)] * (aBoneWeights.w * invW);
    } else {
        skinMat = mat4(1.0);
    }

    vec4 localPos = skinMat * vec4(aPos, 1.0);
    vec4 worldPos = uModel * localPos;
    mat3 m3 = mat3(uModel) * mat3(skinMat);
    vec3 nWorld = m3 * aNormal;
    vWorldPos = worldPos.xyz;
    vWorldNormal = (length(nWorld) > 1e-5) ? normalize(nWorld) : vec3(0.0, 1.0, 0.0);
    vTexCoord = aTexCoord;
    vColor = aColor;
    gl_Position = uViewProj * worldPos;
}
)";

constexpr const char* kSkinnedFS = R"(
#version 330 core
in vec3 vWorldPos;
in vec3 vWorldNormal;
in vec2 vTexCoord;
in vec4 vColor;

uniform vec3 uCamPos;
uniform int uHasDiffuseTex;
uniform sampler2D uDiffuseTex;
uniform float uClayMode;
uniform float uReflection;
uniform vec4 uDirLightDir;
uniform vec4 uDirLightColor;

out vec4 FragColor;

void main() {
    vec3 albedo = (uClayMode > 0.5)
        ? vec3(0.76, 0.74, 0.72)
        : ((uHasDiffuseTex != 0) ? texture(uDiffuseTex, vTexCoord).rgb * ((dot(vColor.rgb, vec3(1.0)) > 0.001) ? vColor.rgb : vec3(1.0)) : vColor.rgb);
    if (dot(albedo, vec3(1.0)) <= 0.001) {
        albedo = vec3(0.8);
    }
    vec3 N = normalize(vWorldNormal);
    vec3 L = normalize(uDirLightDir.xyz);
    vec3 V = normalize(uCamPos - vWorldPos);
    vec3 H = normalize(V + L);
    float halfLam = dot(N, L) * 0.5 + 0.5;
    float spec = pow(max(dot(N, H), 0.0), 24.0) * (0.20 + uReflection * 0.35);
    vec3 color = albedo * (0.35 + uDirLightColor.rgb * halfLam * 0.85) + uDirLightColor.rgb * spec;
    FragColor = vec4(clamp(color, 0.0, 1.0), 1.0);
}
)";

constexpr const char* kSkyVS = R"(
#version 330 core
layout(location = 0) in vec3 aPos;
layout(location = 2) in vec2 aTexCoord;

uniform mat4 uSkyViewProj;

out vec3 vDir;
out vec2 vTexCoord;

void main() {
    vDir = aPos;
    vTexCoord = aTexCoord;
    vec4 pos = uSkyViewProj * vec4(aPos, 1.0);
    gl_Position = pos.xyww;
}
)";

constexpr const char* kSkyFS = R"(
#version 330 core
in vec3 vDir;
in vec2 vTexCoord;

uniform sampler2D uSkyTex;
uniform int uIsCubeAtlas;

out vec4 FragColor;

vec3 SampleSkyAtlas(vec3 d) {
    vec3 ad = abs(d);
    float face = 0.0;
    float u = 0.5;
    float v = 0.5;
    if (ad.x >= ad.y && ad.x >= ad.z) {
        float invM = 1.0 / max(ad.x, 0.00001);
        if (d.x >= 0.0) { face = 0.0; u = -d.z * invM; v = -d.y * invM; }
        else            { face = 1.0; u =  d.z * invM; v = -d.y * invM; }
    } else if (ad.y >= ad.x && ad.y >= ad.z) {
        float invM = 1.0 / max(ad.y, 0.00001);
        if (d.y >= 0.0) { face = 2.0; u =  d.x * invM; v =  d.z * invM; }
        else            { face = 3.0; u =  d.x * invM; v = -d.z * invM; }
    } else {
        float invM = 1.0 / max(ad.z, 0.00001);
        if (d.z >= 0.0) { face = 4.0; u =  d.x * invM; v = -d.y * invM; }
        else            { face = 5.0; u = -d.x * invM; v = -d.y * invM; }
    }
    u = clamp(u * 0.5 + 0.5, 0.004, 0.996);
    v = clamp(v * 0.5 + 0.5, 0.004, 0.996);
    float col = mod(face, 3.0);
    float row = floor(face / 3.0);
    return texture(uSkyTex, vec2((col + u) / 3.0, (row + v) / 2.0)).rgb;
}

void main() {
    vec3 col = (uIsCubeAtlas != 0)
        ? SampleSkyAtlas(normalize(vDir))
        : texture(uSkyTex, vTexCoord).rgb;
    FragColor = vec4(col, 1.0);
}
)";

constexpr const char* kDepthVS = R"(
#version 330 core
layout(location = 0) in vec3 aPos;
uniform mat4 uMVP;
void main() {
    gl_Position = uMVP * vec4(aPos, 1.0);
}
)";

constexpr const char* kDepthFS = R"(
#version 330 core
void main() {
}
)";

GLenum MapDepthFunc(uint8_t compare) noexcept {
    switch (compare) {
        case 1u:                 return GL_LESS_VAL;
        case RHI_COMPARE_EQUAL:  return GL_EQUAL_VAL;
        case RHI_COMPARE_LEQUAL: return GL_LEQUAL_VAL;
        case RHI_COMPARE_ALWAYS: return GL_ALWAYS_VAL;
        default:                 return GL_LEQUAL_VAL;
    }
}

void ApplyGLBlendAndDepthState(bool depthTest,
                               bool depthWrite,
                               uint8_t depthCompare,
                               BlendMode blendMode,
                               bool backfaceCull) noexcept {
    const auto& gl = GetFunctions();
    if (depthTest) {
        gl.Enable(GL_DEPTH_TEST_VAL);
        gl.DepthFunc(MapDepthFunc(depthCompare));
    } else {
        gl.Disable(GL_DEPTH_TEST_VAL);
    }
    gl.DepthMask(depthWrite ? GL_TRUE_VAL : GL_FALSE_VAL);

    if (backfaceCull) {
        gl.Enable(GL_CULL_FACE_VAL);
        gl.CullFace(GL_BACK_VAL);
        gl.FrontFace(GL_CCW_VAL);
    } else {
        gl.Disable(GL_CULL_FACE_VAL);
    }

    if (blendMode == BlendMode::AlphaBlend) {
        gl.Enable(GL_BLEND_VAL);
        gl.BlendFunc(GL_SRC_ALPHA_VAL, GL_ONE_MINUS_SRC_ALPHA_VAL);
    } else if (blendMode == BlendMode::Additive) {
        gl.Enable(GL_BLEND_VAL);
        gl.BlendFunc(GL_SRC_ALPHA_VAL, GL_ONE_VAL);
    } else {
        gl.Disable(GL_BLEND_VAL);
    }
}

void BindTexture2DWithSampler(const GX2Texture* tex,
                              GLuint unit,
                              TextureFilterMode filterMode,
                              TextureWrapMode wrapMode,
                              TextureMipFilterMode mipMode) noexcept {
    const auto& gl = GetFunctions();
    gl.ActiveTexture(GL_TEXTURE0_VAL + unit);
    const GLuint texId = (tex && tex->glTextureId != 0u) ? tex->glTextureId : g_state.whiteFallbackTex;
    gl.BindTexture(GL_TEXTURE_2D_VAL, texId);

    if (tex && tex->glTextureId != 0u) {
        const bool isPoint = (filterMode == TextureFilterMode::Point);
        const bool hasMips = (tex->mipLevels > 1u) && (mipMode != TextureMipFilterMode::None);

        GLint minFilter = static_cast<GLint>(isPoint ? GL_NEAREST_VAL : GL_LINEAR_VAL);
        if (hasMips) {
            if (isPoint) {
                minFilter = static_cast<GLint>(GL_NEAREST_MIPMAP_NEAREST_VAL);
            } else {
                minFilter = static_cast<GLint>(
                    (mipMode == TextureMipFilterMode::Point) ? GL_NEAREST_MIPMAP_LINEAR_VAL
                                                             : GL_LINEAR_MIPMAP_LINEAR_VAL);
            }
        }
        const GLint magFilter = static_cast<GLint>(isPoint ? GL_NEAREST_VAL : GL_LINEAR_VAL);

        GLint wrap = static_cast<GLint>(GL_REPEAT_VAL);
        if (tex->isCubeAtlas || wrapMode == TextureWrapMode::Clamp) {
            wrap = static_cast<GLint>(GL_CLAMP_TO_EDGE_VAL);
        } else if (wrapMode == TextureWrapMode::Mirror) {
            wrap = static_cast<GLint>(GL_MIRRORED_REPEAT_VAL);
        }

        gl.TexParameteri(GL_TEXTURE_2D_VAL, GL_TEXTURE_MIN_FILTER_VAL, minFilter);
        gl.TexParameteri(GL_TEXTURE_2D_VAL, GL_TEXTURE_MAG_FILTER_VAL, magFilter);
        gl.TexParameteri(GL_TEXTURE_2D_VAL, GL_TEXTURE_WRAP_S_VAL, wrap);
        gl.TexParameteri(GL_TEXTURE_2D_VAL, GL_TEXTURE_WRAP_T_VAL, wrap);
    }
    if (unit != 0u) {
        gl.ActiveTexture(GL_TEXTURE0_VAL);
    }
}

void BindStaticVertexAttributes(GLuint vboId, GLsizei stride) noexcept {
    const auto& gl = GetFunctions();
    gl.BindBuffer(GL_ARRAY_BUFFER_VAL, vboId);
    gl.EnableVertexAttribArray(0u);
    gl.VertexAttribPointer(0u, 3, GL_FLOAT_VAL, GL_FALSE_VAL, stride,
                           reinterpret_cast<const void*>(offsetof(MeshVertex, pos)));
    gl.EnableVertexAttribArray(1u);
    gl.VertexAttribPointer(1u, 3, GL_FLOAT_VAL, GL_FALSE_VAL, stride,
                           reinterpret_cast<const void*>(offsetof(MeshVertex, normal)));
    gl.EnableVertexAttribArray(2u);
    gl.VertexAttribPointer(2u, 2, GL_FLOAT_VAL, GL_FALSE_VAL, stride,
                           reinterpret_cast<const void*>(offsetof(MeshVertex, texCoord)));
    gl.EnableVertexAttribArray(3u);
    gl.VertexAttribPointer(3u, 4, GL_FLOAT_VAL, GL_FALSE_VAL, stride,
                           reinterpret_cast<const void*>(offsetof(MeshVertex, color)));
}

void UploadLightUniformsMesh(const LightBlockUBO& lights,
                             GLint dirLoc, GLint dirColLoc,
                             GLint ptCntLoc, GLint ptPosLoc, GLint ptColLoc,
                             GLint spPosLoc, GLint spDirLoc, GLint spColLoc, GLint spIntLoc) noexcept {
    const auto& gl = GetFunctions();
    if (dirLoc >= 0)    gl.Uniform4fv(dirLoc, 1, lights.dirLightDir);
    if (dirColLoc >= 0) gl.Uniform4fv(dirColLoc, 1, lights.dirLightColor);
    const GLint ptCount = static_cast<GLint>(std::min<uint32_t>(lights.activePointLights, 8u));
    if (ptCntLoc >= 0)  gl.Uniform1i(ptCntLoc, ptCount);
    if (ptCount > 0) {
        if (ptPosLoc >= 0) gl.Uniform4fv(ptPosLoc, ptCount, &lights.pointLightPosRadius[0][0]);
        if (ptColLoc >= 0) gl.Uniform4fv(ptColLoc, ptCount, &lights.pointLightColorIntensity[0][0]);
    }
    if (spPosLoc >= 0) gl.Uniform4fv(spPosLoc, 1, lights.spotLightPosRange);
    if (spDirLoc >= 0) gl.Uniform4fv(spDirLoc, 1, lights.spotLightDirInner);
    if (spColLoc >= 0) gl.Uniform4fv(spColLoc, 1, lights.spotLightColorOuter);
    if (spIntLoc >= 0) gl.Uniform1f(spIntLoc, lights.spotLightIntensity);
}

void DrawMeshInternal(const GX2RBuffer& vbo,
                      const GX2RBuffer& ibo,
                      uint32_t indexCount,
                      uint32_t vertexStride,
                      const Matrix4x4& modelMatrix,
                      const Matrix4x4& vpMatrix,
                      const Matrix4x4& shadowLightVP,
                      uint32_t shadowMode,
                      float shadowBias,
                      float shadowTexelSize,
                      float shadowMinLit,
                      float shaderTime,
                      const Point3D& camPos,
                      const GX2Texture* diffuseTex,
                      const GX2Texture* envCubeMap,
                      const MaterialPropertyBlock& mat,
                      uint32_t shadingMode,
                      bool use32BitIndices,
                      uint8_t depthCompare,
                      TextureFilterMode filterMode,
                      TextureWrapMode wrapMode,
                      TextureMipFilterMode mipMode,
                      uint32_t& drawCalls,
                      uint32_t& triangles) noexcept {
    if (!IsLoaded() || g_state.meshProgram == 0u || vbo.glBufferId == 0u || ibo.glBufferId == 0u ||
        indexCount == 0u || vertexStride < sizeof(MeshVertex)) {
        return;
    }

    const auto& gl = GetFunctions();
    gl.UseProgram(g_state.meshProgram);

    gl.UniformMatrix4fv(g_state.mModel, 1, GL_FALSE_VAL, modelMatrix.m);
    gl.UniformMatrix4fv(g_state.mViewProj, 1, GL_FALSE_VAL, vpMatrix.m);
    gl.UniformMatrix4fv(g_state.mLightVP, 1, GL_FALSE_VAL, shadowLightVP.m);
    gl.Uniform3f(g_state.mCamPos, camPos.x, camPos.y, camPos.z);

    gl.Uniform4fv(g_state.mAlbedoTint, 1, mat.ubo.albedoTint);
    const bool hasDiffuse = (diffuseTex != nullptr) && (diffuseTex->glTextureId != 0u);
    const bool hasEnv = (envCubeMap != nullptr) && (envCubeMap->glTextureId != 0u);
    const bool shadowActive = (shadowMode > 0u) && ShadowMapRendererGX2::Instance().IsReady() && (shadingMode <= 3u);

    gl.Uniform1i(g_state.mHasDiffuse, hasDiffuse ? 1 : 0);
    gl.Uniform1i(g_state.mDiffuseTex, 0);
    gl.Uniform1i(g_state.mHasEnv, hasEnv ? 1 : 0);
    gl.Uniform1i(g_state.mEnvTex, 1);
    gl.Uniform1i(g_state.mShadowTex, 2);

    gl.Uniform1f(g_state.mUvTiling, (mat.ubo.pbrParams[2] > 0.001f) ? mat.ubo.pbrParams[2] : 1.0f);
    gl.Uniform1f(g_state.mAlphaCutout, (mat.blendMode == BlendMode::AlphaCutout) ? 1.0f : 0.0f);
    gl.Uniform1f(g_state.mCustomParam, mat.ubo.pbrParams[3]);
    gl.Uniform1f(g_state.mShaderTime, shaderTime);
    gl.Uniform4fv(g_state.mMatParams, 1, mat.ubo.materialParams);
    gl.Uniform4f(g_state.mShadowParams,
                 shadowActive ? static_cast<float>(shadowMode) : 0.0f,
                 shadowBias, shadowTexelSize, shadowMinLit);
    gl.Uniform1f(g_state.mRoughness, mat.ubo.pbrParams[0]);
    gl.Uniform1f(g_state.mMetallic, mat.ubo.pbrParams[1]);
    gl.Uniform3f(g_state.mEmissive, mat.ubo.emissive[0], mat.ubo.emissive[1], mat.ubo.emissive[2]);
    gl.Uniform1i(g_state.mShadingMode, static_cast<GLint>(shadingMode));

    UploadLightUniformsMesh(MaterialRegistry::Instance().GetActiveLightBlock(),
                            g_state.mDirLightDir, g_state.mDirLightColor,
                            g_state.mPointCount, g_state.mPointPosRad, g_state.mPointColInt,
                            g_state.mSpotPosRange, g_state.mSpotDirInner, g_state.mSpotColOuter, g_state.mSpotInt);

    BindTexture2DWithSampler(hasDiffuse ? diffuseTex : nullptr, 0u, filterMode, wrapMode, mipMode);
    BindTexture2DWithSampler(hasEnv ? envCubeMap : nullptr, 1u,
                             TextureFilterMode::Bilinear, TextureWrapMode::Clamp, TextureMipFilterMode::None);
    if (shadowActive) {
        ShadowMapRendererGX2::Instance().BindShadowMapRaw(2u, 2u);
    } else {
        BindTexture2DWithSampler(nullptr, 2u, TextureFilterMode::Point, TextureWrapMode::Clamp, TextureMipFilterMode::None);
    }

    const bool effectiveCull = (shadingMode == 6u) ? false : mat.backfaceCull;
    ApplyGLBlendAndDepthState(mat.depthTest, mat.depthWrite, depthCompare, mat.blendMode, effectiveCull);

    gl.BindVertexArray(g_state.staticVao);
    BindStaticVertexAttributes(vbo.glBufferId, static_cast<GLsizei>(vertexStride));
    gl.BindBuffer(GL_ELEMENT_ARRAY_BUFFER_VAL, ibo.glBufferId);
    gl.DrawElements(GL_TRIANGLES_VAL,
                    static_cast<GLsizei>(indexCount),
                    use32BitIndices ? GL_UNSIGNED_INT_VAL : GL_UNSIGNED_SHORT_VAL,
                    nullptr);

    drawCalls += 1u;
    triangles += indexCount / 3u;
}

} // namespace

ForwardRendererGX2& ForwardRendererGX2::Instance() noexcept {
    static ForwardRendererGX2 s_instance;
    return s_instance;
}

bool ForwardRendererGX2::Initialize() {
    if (m_initialized) return true;
    if (!IsLoaded() && !LoadFunctions()) {
        return false;
    }

    const auto& gl = GetFunctions();
    g_state.meshProgram  = LinkProgram(kMeshVS, kMeshFS);
    g_state.instProgram  = LinkProgram(kInstVS, kMeshFS);
    g_state.skelProgram  = LinkProgram(kSkinnedVS, kSkinnedFS);
    g_state.skyProgram   = LinkProgram(kSkyVS, kSkyFS);
    g_state.depthProgram = LinkProgram(kDepthVS, kDepthFS);

    if (g_state.meshProgram == 0u || g_state.instProgram == 0u ||
        g_state.skelProgram == 0u || g_state.skyProgram == 0u || g_state.depthProgram == 0u) {
        Shutdown();
        return false;
    }

    const GLuint mp = g_state.meshProgram;
    g_state.mModel         = gl.GetUniformLocation(mp, "uModel");
    g_state.mViewProj      = gl.GetUniformLocation(mp, "uViewProj");
    g_state.mLightVP       = gl.GetUniformLocation(mp, "uLightVP");
    g_state.mCamPos        = gl.GetUniformLocation(mp, "uCamPos");
    g_state.mAlbedoTint    = gl.GetUniformLocation(mp, "uAlbedoTint");
    g_state.mHasDiffuse    = gl.GetUniformLocation(mp, "uHasDiffuseTex");
    g_state.mDiffuseTex    = gl.GetUniformLocation(mp, "uDiffuseTex");
    g_state.mHasEnv        = gl.GetUniformLocation(mp, "uHasEnvTex");
    g_state.mEnvTex        = gl.GetUniformLocation(mp, "uEnvTex");
    g_state.mShadowTex     = gl.GetUniformLocation(mp, "uShadowMap");
    g_state.mUvTiling      = gl.GetUniformLocation(mp, "uUvTiling");
    g_state.mAlphaCutout   = gl.GetUniformLocation(mp, "uAlphaCutout");
    g_state.mCustomParam   = gl.GetUniformLocation(mp, "uCustomParam");
    g_state.mShaderTime    = gl.GetUniformLocation(mp, "uShaderTime");
    g_state.mMatParams     = gl.GetUniformLocation(mp, "uMatParams");
    g_state.mShadowParams  = gl.GetUniformLocation(mp, "uShadowParams");
    g_state.mRoughness     = gl.GetUniformLocation(mp, "uRoughness");
    g_state.mMetallic      = gl.GetUniformLocation(mp, "uMetallic");
    g_state.mEmissive      = gl.GetUniformLocation(mp, "uEmissive");
    g_state.mShadingMode   = gl.GetUniformLocation(mp, "uShadingMode");
    g_state.mDirLightDir   = gl.GetUniformLocation(mp, "uDirLightDir");
    g_state.mDirLightColor = gl.GetUniformLocation(mp, "uDirLightColor");
    g_state.mPointCount    = gl.GetUniformLocation(mp, "uPointLightCount");
    g_state.mPointPosRad   = gl.GetUniformLocation(mp, "uPointLightPosRadius");
    g_state.mPointColInt   = gl.GetUniformLocation(mp, "uPointLightColorIntensity");
    g_state.mSpotPosRange  = gl.GetUniformLocation(mp, "uSpotPosRange");
    g_state.mSpotDirInner  = gl.GetUniformLocation(mp, "uSpotDirInner");
    g_state.mSpotColOuter  = gl.GetUniformLocation(mp, "uSpotColorOuter");
    g_state.mSpotInt       = gl.GetUniformLocation(mp, "uSpotIntensity");

    const GLuint ip = g_state.instProgram;
    g_state.iViewProj      = gl.GetUniformLocation(ip, "uViewProj");
    g_state.iLightVP       = gl.GetUniformLocation(ip, "uLightVP");
    g_state.iCamPos        = gl.GetUniformLocation(ip, "uCamPos");
    g_state.iAlbedoTint    = gl.GetUniformLocation(ip, "uAlbedoTint");
    g_state.iHasDiffuse    = gl.GetUniformLocation(ip, "uHasDiffuseTex");
    g_state.iDiffuseTex    = gl.GetUniformLocation(ip, "uDiffuseTex");
    g_state.iHasEnv        = gl.GetUniformLocation(ip, "uHasEnvTex");
    g_state.iEnvTex        = gl.GetUniformLocation(ip, "uEnvTex");
    g_state.iShadowTex     = gl.GetUniformLocation(ip, "uShadowMap");
    g_state.iUvTiling      = gl.GetUniformLocation(ip, "uUvTiling");
    g_state.iAlphaCutout   = gl.GetUniformLocation(ip, "uAlphaCutout");
    g_state.iCustomParam   = gl.GetUniformLocation(ip, "uCustomParam");
    g_state.iShaderTime    = gl.GetUniformLocation(ip, "uShaderTime");
    g_state.iMatParams     = gl.GetUniformLocation(ip, "uMatParams");
    g_state.iShadowParams  = gl.GetUniformLocation(ip, "uShadowParams");
    g_state.iRoughness     = gl.GetUniformLocation(ip, "uRoughness");
    g_state.iMetallic      = gl.GetUniformLocation(ip, "uMetallic");
    g_state.iEmissive      = gl.GetUniformLocation(ip, "uEmissive");
    g_state.iShadingMode   = gl.GetUniformLocation(ip, "uShadingMode");
    g_state.iDirLightDir   = gl.GetUniformLocation(ip, "uDirLightDir");
    g_state.iDirLightColor = gl.GetUniformLocation(ip, "uDirLightColor");
    g_state.iPointCount    = gl.GetUniformLocation(ip, "uPointLightCount");
    g_state.iPointPosRad   = gl.GetUniformLocation(ip, "uPointLightPosRadius");
    g_state.iPointColInt   = gl.GetUniformLocation(ip, "uPointLightColorIntensity");
    g_state.iSpotPosRange  = gl.GetUniformLocation(ip, "uSpotPosRange");
    g_state.iSpotDirInner  = gl.GetUniformLocation(ip, "uSpotDirInner");
    g_state.iSpotColOuter  = gl.GetUniformLocation(ip, "uSpotColorOuter");
    g_state.iSpotInt       = gl.GetUniformLocation(ip, "uSpotIntensity");

    const GLuint sp = g_state.skelProgram;
    g_state.sModel         = gl.GetUniformLocation(sp, "uModel");
    g_state.sViewProj      = gl.GetUniformLocation(sp, "uViewProj");
    g_state.sCamPos        = gl.GetUniformLocation(sp, "uCamPos");
    g_state.sBones         = gl.GetUniformLocation(sp, "uBones");
    g_state.sBoneCount     = gl.GetUniformLocation(sp, "uBoneCount");
    g_state.sHasDiffuse    = gl.GetUniformLocation(sp, "uHasDiffuseTex");
    g_state.sDiffuseTex    = gl.GetUniformLocation(sp, "uDiffuseTex");
    g_state.sClayMode      = gl.GetUniformLocation(sp, "uClayMode");
    g_state.sReflection    = gl.GetUniformLocation(sp, "uReflection");
    g_state.sDirLightDir   = gl.GetUniformLocation(sp, "uDirLightDir");
    g_state.sDirLightColor = gl.GetUniformLocation(sp, "uDirLightColor");

    const GLuint kp = g_state.skyProgram;
    g_state.kSkyVP         = gl.GetUniformLocation(kp, "uSkyViewProj");
    g_state.kSkyTex        = gl.GetUniformLocation(kp, "uSkyTex");
    g_state.kIsCubeAtlas   = gl.GetUniformLocation(kp, "uIsCubeAtlas");

    g_state.dMVP           = gl.GetUniformLocation(g_state.depthProgram, "uMVP");

    gl.GenVertexArrays(1, &g_state.staticVao);
    gl.GenVertexArrays(1, &g_state.instVao);
    gl.GenVertexArrays(1, &g_state.skinnedVao);
    gl.GenVertexArrays(1, &g_state.skyVao);
    gl.GenVertexArrays(1, &g_state.depthVao);
    gl.GenBuffers(1, &g_state.instanceVbo);

    gl.BindBuffer(GL_ARRAY_BUFFER_VAL, g_state.instanceVbo);
    gl.BufferData(GL_ARRAY_BUFFER_VAL,
                  static_cast<GLsizeiptr>(MAX_INSTANCES_PER_DRAW * sizeof(Matrix4x4)),
                  nullptr,
                  GL_DYNAMIC_DRAW_VAL);
    gl.BindBuffer(GL_ARRAY_BUFFER_VAL, 0u);

    static constexpr uint8_t kWhite[4] = {255u, 255u, 255u, 255u};
    gl.GenTextures(1, &g_state.whiteFallbackTex);
    gl.BindTexture(GL_TEXTURE_2D_VAL, g_state.whiteFallbackTex);
    if (gl.PixelStorei) gl.PixelStorei(GL_UNPACK_ALIGNMENT_VAL, 1);
    gl.TexImage2D(GL_TEXTURE_2D_VAL, 0, static_cast<GLint>(GL_RGBA8_VAL),
                  1, 1, 0, GL_RGBA_VAL, GL_UNSIGNED_BYTE_VAL, kWhite);
    gl.TexParameteri(GL_TEXTURE_2D_VAL, GL_TEXTURE_MIN_FILTER_VAL, static_cast<GLint>(GL_NEAREST_VAL));
    gl.TexParameteri(GL_TEXTURE_2D_VAL, GL_TEXTURE_MAG_FILTER_VAL, static_cast<GLint>(GL_NEAREST_VAL));
    gl.BindTexture(GL_TEXTURE_2D_VAL, 0u);

    ApplyTextureFilterMode(EngineConfig::Instance().graphics.textureFilter);
    m_initialized = true;
    return true;
}

void ForwardRendererGX2::Shutdown() {
    if (IsLoaded()) {
        const auto& gl = GetFunctions();
        if (gl.DeleteTextures && g_state.whiteFallbackTex != 0u) {
            gl.DeleteTextures(1, &g_state.whiteFallbackTex);
        }
        if (gl.DeleteBuffers && g_state.instanceVbo != 0u) {
            gl.DeleteBuffers(1, &g_state.instanceVbo);
        }
        if (gl.DeleteVertexArrays) {
            if (g_state.staticVao != 0u)  gl.DeleteVertexArrays(1, &g_state.staticVao);
            if (g_state.instVao != 0u)    gl.DeleteVertexArrays(1, &g_state.instVao);
            if (g_state.skinnedVao != 0u) gl.DeleteVertexArrays(1, &g_state.skinnedVao);
            if (g_state.skyVao != 0u)     gl.DeleteVertexArrays(1, &g_state.skyVao);
            if (g_state.depthVao != 0u)   gl.DeleteVertexArrays(1, &g_state.depthVao);
        }
        if (gl.DeleteProgram) {
            if (g_state.meshProgram != 0u)  gl.DeleteProgram(g_state.meshProgram);
            if (g_state.instProgram != 0u)  gl.DeleteProgram(g_state.instProgram);
            if (g_state.skelProgram != 0u)  gl.DeleteProgram(g_state.skelProgram);
            if (g_state.skyProgram != 0u)   gl.DeleteProgram(g_state.skyProgram);
            if (g_state.depthProgram != 0u) gl.DeleteProgram(g_state.depthProgram);
        }
    }
    g_state = GLRendererState{};
    m_initialized = false;
}

void ForwardRendererGX2::BeginFrame(const Point3D& camPos, const Matrix4x4& viewMatrix, const Matrix4x4& projMatrix) {
    m_camPos = camPos;
    m_viewMatrix = viewMatrix;
    m_projMatrix = projMatrix;
    m_vpMatrix = projMatrix * viewMatrix;
    m_drawCallsCount = 0u;
    m_trianglesCount = 0u;
    m_instancedDrawCalls = 0u;
    m_instancedCount = 0u;
    m_shadowMode = 0u;
    g_state.inDepthPrePass = false;

    if (!m_initialized) {
        (void)Initialize();
    }
    if (IsLoaded()) {
        const auto& gl = GetFunctions();
        gl.ColorMask(GL_TRUE_VAL, GL_TRUE_VAL, GL_TRUE_VAL, GL_TRUE_VAL);
        gl.Enable(GL_DEPTH_TEST_VAL);
        gl.DepthMask(GL_TRUE_VAL);
        gl.DepthFunc(GL_LEQUAL_VAL);
        gl.Disable(GL_BLEND_VAL);
    }
}

void ForwardRendererGX2::EndFrame() {
    if (!IsLoaded()) return;
    const auto& gl = GetFunctions();
    gl.ActiveTexture(GL_TEXTURE2_VAL);
    gl.BindTexture(GL_TEXTURE_2D_VAL, 0u);
    gl.ActiveTexture(GL_TEXTURE1_VAL);
    gl.BindTexture(GL_TEXTURE_2D_VAL, 0u);
    gl.ActiveTexture(GL_TEXTURE0_VAL);
    gl.BindTexture(GL_TEXTURE_2D_VAL, 0u);
    gl.BindVertexArray(0u);
    gl.BindBuffer(GL_ARRAY_BUFFER_VAL, 0u);
    gl.UseProgram(0u);
    gl.ColorMask(GL_TRUE_VAL, GL_TRUE_VAL, GL_TRUE_VAL, GL_TRUE_VAL);
    gl.DepthMask(GL_TRUE_VAL);
    gl.DepthFunc(GL_LESS_VAL);
    gl.Disable(GL_CULL_FACE_VAL);
    gl.Disable(GL_BLEND_VAL);
}

void ForwardRendererGX2::RenderSkydome(StaticMesh& skydomeMesh, GX2Texture* skyTexture) {
    if (!m_initialized || !IsLoaded() || !skydomeMesh.IsLoaded() || !skyTexture ||
        !skydomeMesh.GetVBO() || !skydomeMesh.GetIBO() || skydomeMesh.GetIndexCount() == 0u) {
        return;
    }

    const GX2RBuffer& vbo = *skydomeMesh.GetVBO();
    const GX2RBuffer& ibo = *skydomeMesh.GetIBO();
    if (vbo.glBufferId == 0u || ibo.glBufferId == 0u || g_state.skyProgram == 0u) {
        return;
    }

    const auto& gl = GetFunctions();
    Matrix4x4 skyView = m_viewMatrix;
    skyView.m[12] = 0.0f;
    skyView.m[13] = 0.0f;
    skyView.m[14] = 0.0f;
    const Matrix4x4 skyVP = m_projMatrix * skyView;

    gl.UseProgram(g_state.skyProgram);
    gl.UniformMatrix4fv(g_state.kSkyVP, 1, GL_FALSE_VAL, skyVP.m);
    gl.Uniform1i(g_state.kSkyTex, 0);
    gl.Uniform1i(g_state.kIsCubeAtlas, skyTexture->isCubeAtlas ? 1 : 0);

    BindTexture2DWithSampler(skyTexture, 0u, TextureFilterMode::Bilinear, TextureWrapMode::Clamp, TextureMipFilterMode::None);
    ApplyGLBlendAndDepthState(true, false, RHI_COMPARE_LEQUAL, BlendMode::Opaque, false);

    gl.BindVertexArray(g_state.skyVao);
    gl.BindBuffer(GL_ARRAY_BUFFER_VAL, vbo.glBufferId);
    const GLsizei stride = static_cast<GLsizei>(skydomeMesh.GetHeader().vertexStride);
    gl.EnableVertexAttribArray(0u);
    gl.VertexAttribPointer(0u, 3, GL_FLOAT_VAL, GL_FALSE_VAL, stride,
                           reinterpret_cast<const void*>(offsetof(MeshVertex, pos)));
    gl.EnableVertexAttribArray(2u);
    gl.VertexAttribPointer(2u, 2, GL_FLOAT_VAL, GL_FALSE_VAL, stride,
                           reinterpret_cast<const void*>(offsetof(MeshVertex, texCoord)));

    const bool isU32 = (skydomeMesh.GetHeader().indexFormat == 1u);
    gl.BindBuffer(GL_ELEMENT_ARRAY_BUFFER_VAL, ibo.glBufferId);
    gl.DrawElements(GL_TRIANGLES_VAL,
                    static_cast<GLsizei>(skydomeMesh.GetIndexCount()),
                    isU32 ? GL_UNSIGNED_INT_VAL : GL_UNSIGNED_SHORT_VAL,
                    nullptr);

    m_drawCallsCount += 1u;
    m_trianglesCount += skydomeMesh.GetIndexCount() / 3u;
}

void ForwardRendererGX2::RenderStaticMesh(const StaticMesh& mesh,
                                          const Matrix4x4& modelMatrix,
                                          GX2Texture* diffuseTexture,
                                          GX2Texture* envCubeMap,
                                          float reflectionIntensity,
                                          bool normalMapEnabled,
                                          float bumpScale,
                                          float ambientIntensity) {
    if (!m_initialized || !mesh.IsLoaded() || !mesh.GetVBO() || !mesh.GetIBO()) return;

    MaterialPropertyBlock mat{};
    mat.diffuseTexture = diffuseTexture;
    mat.envCubeMap = envCubeMap;
    mat.ubo.materialParams[0] = reflectionIntensity;
    mat.ubo.materialParams[1] = normalMapEnabled ? 1.0f : 0.0f;
    mat.ubo.materialParams[2] = bumpScale;
    mat.ubo.materialParams[3] = ambientIntensity;
    mat.backfaceCull = false;

    DrawMeshInternal(*mesh.GetVBO(), *mesh.GetIBO(), mesh.GetIndexCount(), mesh.GetHeader().vertexStride,
                     modelMatrix, m_vpMatrix, m_shadowLightVP, m_shadowMode,
                     m_shadowDepthBias, m_shadowTexelSize, m_shadowMinLit, m_shaderTime,
                     m_camPos, diffuseTexture, envCubeMap, mat, 1u,
                     mesh.GetHeader().indexFormat == 1u, RHI_COMPARE_LEQUAL,
                     m_activeFilterMode, m_activeWrapMode, m_activeMipFilterMode,
                     m_drawCallsCount, m_trianglesCount);
}

void ForwardRendererGX2::RenderStaticMeshWithMaterial(const StaticMesh& mesh,
                                                      const Matrix4x4& modelMatrix,
                                                      const MaterialPropertyBlock& material) {
    if (!m_initialized || !mesh.IsLoaded() || !mesh.GetVBO() || !mesh.GetIBO()) return;
    DrawMeshInternal(*mesh.GetVBO(), *mesh.GetIBO(), mesh.GetIndexCount(), mesh.GetHeader().vertexStride,
                     modelMatrix, m_vpMatrix, m_shadowLightVP, m_shadowMode,
                     m_shadowDepthBias, m_shadowTexelSize, m_shadowMinLit, m_shaderTime,
                     m_camPos,
                     static_cast<const GX2Texture*>(material.diffuseTexture),
                     static_cast<const GX2Texture*>(material.envCubeMap),
                     material, 1u, mesh.GetHeader().indexFormat == 1u, RHI_COMPARE_LEQUAL,
                     m_activeFilterMode, m_activeWrapMode, m_activeMipFilterMode,
                     m_drawCallsCount, m_trianglesCount);
}

void ForwardRendererGX2::RenderStaticMeshPBR(const StaticMesh& mesh,
                                             const Matrix4x4& modelMatrix,
                                             const MaterialPropertyBlock& material,
                                             uint32_t shadingMode) {
    if (!m_initialized || !mesh.IsLoaded() || !mesh.GetVBO() || !mesh.GetIBO()) return;
    DrawMeshInternal(*mesh.GetVBO(), *mesh.GetIBO(), mesh.GetIndexCount(), mesh.GetHeader().vertexStride,
                     modelMatrix, m_vpMatrix, m_shadowLightVP, m_shadowMode,
                     m_shadowDepthBias, m_shadowTexelSize, m_shadowMinLit, m_shaderTime,
                     m_camPos,
                     static_cast<const GX2Texture*>(material.diffuseTexture),
                     static_cast<const GX2Texture*>(material.envCubeMap),
                     material, shadingMode, mesh.GetHeader().indexFormat == 1u, RHI_COMPARE_LEQUAL,
                     m_activeFilterMode, m_activeWrapMode, m_activeMipFilterMode,
                     m_drawCallsCount, m_trianglesCount);
}

void ForwardRendererGX2::SetDirectionalShadowState(const Matrix4x4& lightVP,
                                                   uint32_t shadowMode,
                                                   float depthBias,
                                                   float texelSize,
                                                   float minLitFactor) noexcept {
    m_shadowLightVP = lightVP;
    m_shadowMode = shadowMode;
    m_shadowDepthBias = depthBias;
    m_shadowTexelSize = (texelSize > 0.0f) ? texelSize : (1.0f / 1024.0f);
    m_shadowMinLit = minLitFactor;
}

void ForwardRendererGX2::RenderMeshBuffers(GX2RBuffer& vbo,
                                           GX2RBuffer& ibo,
                                           uint32_t indexCount,
                                           uint32_t vertexStride,
                                           const Matrix4x4& modelMatrix,
                                           GX2Texture* diffuseTexture,
                                           GX2Texture* envCubeMap,
                                           float reflectionIntensity,
                                           bool normalMapEnabled,
                                           float bumpScale,
                                           float ambientIntensity,
                                           bool use32BitIndices,
                                           BlendMode blendMode,
                                           bool depthTest,
                                           bool depthWrite,
                                           uint8_t depthCompare) {
    if (!m_initialized) return;

    MaterialPropertyBlock mat{};
    mat.diffuseTexture = diffuseTexture;
    mat.envCubeMap = envCubeMap;
    mat.blendMode = blendMode;
    mat.depthTest = depthTest;
    mat.depthWrite = depthWrite;
    mat.backfaceCull = false;
    mat.ubo.materialParams[0] = reflectionIntensity;
    mat.ubo.materialParams[1] = normalMapEnabled ? 1.0f : 0.0f;
    mat.ubo.materialParams[2] = bumpScale;
    mat.ubo.materialParams[3] = ambientIntensity;

    DrawMeshInternal(vbo, ibo, indexCount, vertexStride, modelMatrix, m_vpMatrix,
                     m_shadowLightVP, m_shadowMode, m_shadowDepthBias, m_shadowTexelSize, m_shadowMinLit, m_shaderTime,
                     m_camPos, diffuseTexture, envCubeMap, mat, 1u, use32BitIndices, depthCompare,
                     m_activeFilterMode, m_activeWrapMode, m_activeMipFilterMode,
                     m_drawCallsCount, m_trianglesCount);
}

void ForwardRendererGX2::RenderStaticMeshInstanced(const StaticMesh& mesh,
                                                   const Matrix4x4* modelMatrices,
                                                   uint32_t instanceCount,
                                                   GX2Texture* diffuseTexture,
                                                   GX2Texture* envCubeMap,
                                                   const MaterialPropertyBlock* material) {
    if (!m_initialized || !IsLoaded() || !mesh.IsLoaded() || !modelMatrices || instanceCount == 0u ||
        !mesh.GetVBO() || !mesh.GetIBO() || g_state.instProgram == 0u) {
        return;
    }

    const GX2RBuffer& vbo = *mesh.GetVBO();
    const GX2RBuffer& ibo = *mesh.GetIBO();
    if (vbo.glBufferId == 0u || ibo.glBufferId == 0u) return;

    const auto& gl = GetFunctions();
    const LightBlockUBO& lights = MaterialRegistry::Instance().GetActiveLightBlock();

    gl.UseProgram(g_state.instProgram);
    gl.UniformMatrix4fv(g_state.iViewProj, 1, GL_FALSE_VAL, m_vpMatrix.m);
    gl.UniformMatrix4fv(g_state.iLightVP, 1, GL_FALSE_VAL, m_shadowLightVP.m);
    gl.Uniform3f(g_state.iCamPos, m_camPos.x, m_camPos.y, m_camPos.z);

    const float fallbackTint[4] = {1.0f, 1.0f, 1.0f, 1.0f};
    const float defaultMat[4] = {0.0f, 1.0f, 2.0f, 0.38f};
    const bool hasDiffuse = (diffuseTexture != nullptr) && (diffuseTexture->glTextureId != 0u);
    const bool hasEnv = (envCubeMap != nullptr) && (envCubeMap->glTextureId != 0u);

    gl.Uniform4fv(g_state.iAlbedoTint, 1, material ? material->ubo.albedoTint : fallbackTint);
    gl.Uniform1i(g_state.iHasDiffuse, hasDiffuse ? 1 : 0);
    gl.Uniform1i(g_state.iDiffuseTex, 0);
    gl.Uniform1i(g_state.iHasEnv, hasEnv ? 1 : 0);
    gl.Uniform1i(g_state.iEnvTex, 1);
    gl.Uniform1i(g_state.iShadowTex, 2);
    gl.Uniform1f(g_state.iUvTiling, (material && material->ubo.pbrParams[2] > 0.001f) ? material->ubo.pbrParams[2] : 1.0f);
    gl.Uniform1f(g_state.iAlphaCutout, (material && material->blendMode == BlendMode::AlphaCutout) ? 1.0f : 0.0f);
    gl.Uniform1f(g_state.iCustomParam, material ? material->ubo.pbrParams[3] : 0.0f);
    gl.Uniform1f(g_state.iShaderTime, m_shaderTime);
    gl.Uniform4fv(g_state.iMatParams, 1, material ? material->ubo.materialParams : defaultMat);
    gl.Uniform4f(g_state.iShadowParams, 0.0f, 0.0f, m_shadowTexelSize, m_shadowMinLit);
    gl.Uniform1f(g_state.iRoughness, material ? material->ubo.pbrParams[0] : 0.5f);
    gl.Uniform1f(g_state.iMetallic, material ? material->ubo.pbrParams[1] : 0.0f);
    gl.Uniform3f(g_state.iEmissive,
                 material ? material->ubo.emissive[0] : 0.0f,
                 material ? material->ubo.emissive[1] : 0.0f,
                 material ? material->ubo.emissive[2] : 0.0f);
    gl.Uniform1i(g_state.iShadingMode, 1);

    UploadLightUniformsMesh(lights,
                            g_state.iDirLightDir, g_state.iDirLightColor,
                            g_state.iPointCount, g_state.iPointPosRad, g_state.iPointColInt,
                            g_state.iSpotPosRange, g_state.iSpotDirInner, g_state.iSpotColOuter, g_state.iSpotInt);

    BindTexture2DWithSampler(hasDiffuse ? diffuseTexture : nullptr, 0u,
                             m_activeFilterMode, m_activeWrapMode, m_activeMipFilterMode);
    BindTexture2DWithSampler(hasEnv ? envCubeMap : nullptr, 1u,
                             TextureFilterMode::Bilinear, TextureWrapMode::Clamp, TextureMipFilterMode::None);
    BindTexture2DWithSampler(nullptr, 2u, TextureFilterMode::Point, TextureWrapMode::Clamp, TextureMipFilterMode::None);

    ApplyGLBlendAndDepthState(
        material ? material->depthTest : true,
        material ? material->depthWrite : true,
        RHI_COMPARE_LEQUAL,
        material ? material->blendMode : BlendMode::Opaque,
        false);

    gl.BindVertexArray(g_state.instVao);
    BindStaticVertexAttributes(vbo.glBufferId, static_cast<GLsizei>(mesh.GetHeader().vertexStride));

    gl.BindBuffer(GL_ARRAY_BUFFER_VAL, g_state.instanceVbo);
    for (uint32_t col = 0u; col < 4u; ++col) {
        const GLuint loc = 4u + col;
        gl.EnableVertexAttribArray(loc);
        gl.VertexAttribPointer(loc, 4, GL_FLOAT_VAL, GL_FALSE_VAL,
                               static_cast<GLsizei>(sizeof(Matrix4x4)),
                               reinterpret_cast<const void*>(static_cast<uintptr_t>(col * 16u)));
        gl.VertexAttribDivisor(loc, 1u);
    }

    gl.BindBuffer(GL_ELEMENT_ARRAY_BUFFER_VAL, ibo.glBufferId);
    const GLenum idxEnum = (mesh.GetHeader().indexFormat == 1u) ? GL_UNSIGNED_INT_VAL : GL_UNSIGNED_SHORT_VAL;

    uint32_t offset = 0u;
    while (offset < instanceCount) {
        const uint32_t batch = std::min(instanceCount - offset, MAX_INSTANCES_PER_DRAW);
        gl.BufferSubData(GL_ARRAY_BUFFER_VAL, 0,
                         static_cast<GLsizeiptr>(batch * sizeof(Matrix4x4)),
                         modelMatrices + offset);
        gl.DrawElementsInstanced(GL_TRIANGLES_VAL,
                                 static_cast<GLsizei>(mesh.GetIndexCount()),
                                 idxEnum,
                                 nullptr,
                                 static_cast<GLsizei>(batch));
        m_drawCallsCount += 1u;
        m_instancedDrawCalls += 1u;
        m_instancedCount += batch;
        m_trianglesCount += (mesh.GetIndexCount() / 3u) * batch;
        offset += batch;
    }
}

void ForwardRendererGX2::RenderSkinnedMesh(const StaticMesh& skinnedMesh,
                                           const Matrix4x4& modelMatrix,
                                           const Matrix4x4* boneMatrices,
                                           uint32_t boneCount,
                                           GX2Texture* diffuseTexture,
                                           GX2Texture* envCubeMap,
                                           float reflectionIntensity,
                                           bool clayMode) {
    if (!skinnedMesh.IsLoaded() || !skinnedMesh.GetVBO() || !skinnedMesh.GetIBO()) return;
    RenderSkinnedMesh(*const_cast<GX2RBuffer*>(skinnedMesh.GetVBO()),
                      *const_cast<GX2RBuffer*>(skinnedMesh.GetIBO()),
                      skinnedMesh.GetIndexCount(),
                      skinnedMesh.GetHeader().vertexStride,
                      modelMatrix,
                      boneMatrices,
                      boneCount,
                      diffuseTexture,
                      envCubeMap,
                      reflectionIntensity,
                      clayMode,
                      skinnedMesh.GetHeader().indexFormat == 1u);
}

void ForwardRendererGX2::RenderSkinnedMesh(GX2RBuffer& vbo,
                                           GX2RBuffer& ibo,
                                           uint32_t indexCount,
                                           uint32_t vertexStride,
                                           const Matrix4x4& modelMatrix,
                                           const Matrix4x4* boneMatrices,
                                           uint32_t boneCount,
                                           GX2Texture* diffuseTexture,
                                           GX2Texture* envCubeMap,
                                           float reflectionIntensity,
                                           bool clayMode,
                                           bool use32BitIndices) {
    (void)envCubeMap;
    if (!m_initialized || !IsLoaded() || g_state.skelProgram == 0u ||
        vbo.glBufferId == 0u || ibo.glBufferId == 0u || indexCount == 0u ||
        vertexStride < SKINNED_VERTEX_STRIDE || !boneMatrices || boneCount == 0u) {
        return;
    }

    const auto& gl = GetFunctions();
    const LightBlockUBO& lights = MaterialRegistry::Instance().GetActiveLightBlock();

    gl.UseProgram(g_state.skelProgram);
    gl.UniformMatrix4fv(g_state.sModel, 1, GL_FALSE_VAL, modelMatrix.m);
    gl.UniformMatrix4fv(g_state.sViewProj, 1, GL_FALSE_VAL, m_vpMatrix.m);
    gl.Uniform3f(g_state.sCamPos, m_camPos.x, m_camPos.y, m_camPos.z);

    const uint32_t safeBones = std::min<uint32_t>(boneCount, 64u);
    gl.Uniform1i(g_state.sBoneCount, static_cast<GLint>(safeBones));
    gl.UniformMatrix4fv(g_state.sBones, static_cast<GLsizei>(safeBones), GL_FALSE_VAL, boneMatrices[0].m);

    const bool hasDiffuse = (diffuseTexture != nullptr) && (diffuseTexture->glTextureId != 0u);
    gl.Uniform1i(g_state.sHasDiffuse, hasDiffuse ? 1 : 0);
    gl.Uniform1i(g_state.sDiffuseTex, 0);
    gl.Uniform1f(g_state.sClayMode, clayMode ? 1.0f : 0.0f);
    gl.Uniform1f(g_state.sReflection, reflectionIntensity);
    gl.Uniform4fv(g_state.sDirLightDir, 1, lights.dirLightDir);
    gl.Uniform4fv(g_state.sDirLightColor, 1, lights.dirLightColor);

    BindTexture2DWithSampler(hasDiffuse ? diffuseTexture : nullptr, 0u,
                             m_activeFilterMode, m_activeWrapMode, m_activeMipFilterMode);
    ApplyGLBlendAndDepthState(true, true, RHI_COMPARE_LEQUAL, BlendMode::Opaque, false);

    gl.BindVertexArray(g_state.skinnedVao);
    BindStaticVertexAttributes(vbo.glBufferId, static_cast<GLsizei>(vertexStride));
    gl.EnableVertexAttribArray(4u);
    gl.VertexAttribIPointer(4u, 4, GL_UNSIGNED_BYTE_VAL, static_cast<GLsizei>(vertexStride),
                            reinterpret_cast<const void*>(offsetof(SkinnedMeshVertex, boneIndices)));
    gl.EnableVertexAttribArray(5u);
    gl.VertexAttribPointer(5u, 4, GL_FLOAT_VAL, GL_FALSE_VAL, static_cast<GLsizei>(vertexStride),
                           reinterpret_cast<const void*>(offsetof(SkinnedMeshVertex, boneWeights)));

    gl.BindBuffer(GL_ELEMENT_ARRAY_BUFFER_VAL, ibo.glBufferId);
    gl.DrawElements(GL_TRIANGLES_VAL,
                    static_cast<GLsizei>(indexCount),
                    use32BitIndices ? GL_UNSIGNED_INT_VAL : GL_UNSIGNED_SHORT_VAL,
                    nullptr);

    m_drawCallsCount += 1u;
    m_trianglesCount += indexCount / 3u;
}

void ForwardRendererGX2::RenderSkinnedMeshInstanced(const StaticMesh& skinnedMesh,
                                                    const Matrix4x4* modelMatrices,
                                                    const Matrix4x4* const* bonePalettes,
                                                    uint32_t boneCount,
                                                    uint32_t instanceCount,
                                                    GX2Texture* diffuseTexture,
                                                    GX2Texture* envCubeMap,
                                                    float reflectionIntensity,
                                                    bool clayMode) {
    if (!modelMatrices || !bonePalettes) return;
    for (uint32_t i = 0u; i < instanceCount; ++i) {
        if (!bonePalettes[i]) continue;
        RenderSkinnedMesh(skinnedMesh, modelMatrices[i], bonePalettes[i], boneCount,
                          diffuseTexture, envCubeMap, reflectionIntensity, clayMode);
    }
}

void ForwardRendererGX2::BeginDepthPrePass() {
    if (!m_initialized || !IsLoaded() || g_state.depthProgram == 0u) return;
    g_state.inDepthPrePass = true;
    const auto& gl = GetFunctions();
    gl.ColorMask(GL_FALSE_VAL, GL_FALSE_VAL, GL_FALSE_VAL, GL_FALSE_VAL);
    gl.Enable(GL_DEPTH_TEST_VAL);
    gl.DepthMask(GL_TRUE_VAL);
    gl.DepthFunc(GL_LEQUAL_VAL);
    gl.Disable(GL_CULL_FACE_VAL);
    gl.Disable(GL_BLEND_VAL);
}

void ForwardRendererGX2::RenderDepthPrePassBuffers(GX2RBuffer& vbo,
                                                   GX2RBuffer& ibo,
                                                   uint32_t indexCount,
                                                   uint32_t vertexStride,
                                                   const Matrix4x4& modelMatrix,
                                                   bool use32BitIndices) {
    if (!g_state.inDepthPrePass || !IsLoaded() || g_state.depthProgram == 0u ||
        vbo.glBufferId == 0u || ibo.glBufferId == 0u || indexCount == 0u) {
        return;
    }

    const auto& gl = GetFunctions();
    const Matrix4x4 mvp = m_vpMatrix * modelMatrix;
    gl.UseProgram(g_state.depthProgram);
    gl.UniformMatrix4fv(g_state.dMVP, 1, GL_FALSE_VAL, mvp.m);

    gl.BindVertexArray(g_state.depthVao);
    gl.BindBuffer(GL_ARRAY_BUFFER_VAL, vbo.glBufferId);
    gl.EnableVertexAttribArray(0u);
    gl.VertexAttribPointer(0u, 3, GL_FLOAT_VAL, GL_FALSE_VAL, static_cast<GLsizei>(vertexStride),
                           reinterpret_cast<const void*>(offsetof(MeshVertex, pos)));
    gl.BindBuffer(GL_ELEMENT_ARRAY_BUFFER_VAL, ibo.glBufferId);
    gl.DrawElements(GL_TRIANGLES_VAL,
                    static_cast<GLsizei>(indexCount),
                    use32BitIndices ? GL_UNSIGNED_INT_VAL : GL_UNSIGNED_SHORT_VAL,
                    nullptr);

    m_drawCallsCount += 1u;
    m_trianglesCount += indexCount / 3u;
}

void ForwardRendererGX2::EndDepthPrePass() {
    if (!g_state.inDepthPrePass || !IsLoaded()) return;
    g_state.inDepthPrePass = false;
    GetFunctions().ColorMask(GL_TRUE_VAL, GL_TRUE_VAL, GL_TRUE_VAL, GL_TRUE_VAL);
}

void ForwardRendererGX2::SetTextureFilterMode(TextureFilterMode mode) {
    m_activeFilterMode = mode;
    m_activeMipFilterMode = (mode == TextureFilterMode::Point)
        ? TextureMipFilterMode::Point
        : TextureMipFilterMode::Linear;
}

void ForwardRendererGX2::SetTextureWrapMode(TextureWrapMode mode) {
    m_activeWrapMode = mode;
}

void ForwardRendererGX2::SetTextureMipFilter(TextureMipFilterMode mode) {
    m_activeMipFilterMode = mode;
}

void ForwardRendererGX2::ResetMeshSamplerDefaults() {
    m_activeWrapMode = TextureWrapMode::Wrap;
    m_activeFilterMode = EngineConfig::Instance().graphics.textureFilter;
    m_activeMipFilterMode = (m_activeFilterMode == TextureFilterMode::Point)
        ? TextureMipFilterMode::Point
        : TextureMipFilterMode::Linear;
}

void ForwardRendererGX2::ApplyTextureFilterMode(TextureFilterMode mode) {
    m_activeFilterMode = mode;
    m_activeMipFilterMode = (mode == TextureFilterMode::Point)
        ? TextureMipFilterMode::Point
        : TextureMipFilterMode::Linear;
}

void ForwardRendererGX2::ApplyMeshSamplerState() {
}

} // namespace Neleo

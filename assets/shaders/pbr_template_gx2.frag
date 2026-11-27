#version 330 core

// ============================================================================
// [OPT-457] Fuente Canonica PBR Parametrizada (Compile-Time Permutations)
// Macros inyectadas por tools/CookShaders.py:
//   USE_MULTILIGHT : 0 (solo direccional, uPsData[6]) | 1 (4 puntuales + spot)
//   USE_NORMAL_MAP : 0 (malla lisa, 0 dFdx/dFdy)      | 1 (bump TBN analitico)
//   USE_SHADOWS    : 0 (sin shadow map)               | 1 (PCF + uShadowMap)
// ============================================================================

#ifndef USE_MULTILIGHT
#define USE_MULTILIGHT 0
#endif
#ifndef USE_NORMAL_MAP
#define USE_NORMAL_MAP 0
#endif
#ifndef USE_SHADOWS
#define USE_SHADOWS 0
#endif

// Sin layout(location=...) para coincidir con pbr_mesh_gx2.vert (#version 330 core)
// y garantizar enlace por nombre en CafeGLSL (SPI_PS_INPUT_CNTL).
in vec3 vWorldPos;
in vec3 vNormal;
in vec2 vTexCoord;
in vec4 vColor;
in vec4 vLightSpacePos;
in vec3 vLocalPos;

out vec4 fragColor;

uniform sampler2D uTexture;
uniform sampler2D uSkybox;
#if USE_SHADOWS
uniform sampler2D uShadowMap;
uniform vec4 uPsData[18];
#elif USE_MULTILIGHT
uniform vec4 uPsData[17];
#else
uniform vec4 uPsData[6];
#endif

const float PI = 3.14159265359;

vec3 SampleCubeAtlas(vec3 dir) {
    vec3 a = abs(dir);
    float sc = 0.0;
    float tc = 0.0;
    float col = 0.0;
    float row = 0.0;

    if (a.x >= a.y && a.x >= a.z) {
        float invM = 1.0 / max(a.x, 0.00001);
        if (dir.x >= 0.0) {
            sc = -dir.z * invM; tc = -dir.y * invM; col = 0.0; row = 0.0;
        } else {
            sc =  dir.z * invM; tc = -dir.y * invM; col = 1.0; row = 0.0;
        }
    } else if (a.y >= a.x && a.y >= a.z) {
        float invM = 1.0 / max(a.y, 0.00001);
        if (dir.y >= 0.0) {
            sc =  dir.x * invM; tc =  dir.z * invM; col = 2.0; row = 0.0;
        } else {
            sc =  dir.x * invM; tc = -dir.z * invM; col = 0.0; row = 1.0;
        }
    } else {
        float invM = 1.0 / max(a.z, 0.00001);
        if (dir.z >= 0.0) {
            sc =  dir.x * invM; tc = -dir.y * invM; col = 1.0; row = 1.0;
        } else {
            sc = -dir.x * invM; tc = -dir.y * invM; col = 2.0; row = 1.0;
        }
    }

    vec2 faceUV = clamp(vec2(sc, tc) * 0.5 + 0.5, 0.004, 0.996);
    vec2 atlasUV = (vec2(col, row) + faceUV) * vec2(1.0 / 3.0, 0.5);
    return texture(uSkybox, atlasUV).rgb;
}

vec3 FresnelSchlick(float cosTheta, vec3 F0) {
    float f = clamp(1.0 - cosTheta, 0.0, 1.0);
    float f2 = f * f;
    return F0 + (1.0 - F0) * (f2 * f2 * f);
}

vec3 ToneMapACES(vec3 x) {
    return clamp((x * (2.51 * x + 0.03)) / (x * (2.43 * x + 0.59) + 0.14), 0.0, 1.0);
}

vec3 EvalPBRLightFast(vec3 N, vec3 V, vec3 L, vec3 radiance, vec3 diffBase, vec3 F0,
                      float a2, float a2m1, float k, float inv1mK, float ggxV, float inv4NdotV) {
    float NdotL = max(dot(N, L), 0.0);
    if (NdotL <= 0.0) return vec3(0.0);

    vec3 H = normalize(V + L);
    float NdotH = max(dot(N, H), 0.0);
    float denom = NdotH * NdotH * a2m1 + 1.0;
    float NDF = a2 / max(PI * denom * denom, 0.0001);
    float ggxL = NdotL / (NdotL * inv1mK + k);
    vec3 F = FresnelSchlick(max(dot(H, V), 0.0), F0);

    vec3 spec = F * (NDF * ggxV * ggxL * inv4NdotV);
    vec3 diff = diffBase * (vec3(1.0) - F) * NdotL;
    return (diff + spec) * radiance;
}

vec3 EvalBlinnPhongLight(vec3 N, vec3 V, vec3 L, vec3 radiance, vec3 albedo, float shininess, vec3 specColor) {
    float NdotL = max(dot(N, L), 0.0);
    if (NdotL <= 0.0) return vec3(0.0);

    vec3 H = normalize(V + L);
    float spec = pow(max(dot(N, H), 0.0), shininess);
    return (albedo * NdotL + specColor * spec) * radiance;
}

#if USE_SHADOWS
float ComputeDirectionalShadow(vec4 lightSpacePos, vec3 N, vec3 L) {
    float shadowMode = uPsData[17].x;
    if (shadowMode < 0.5) return 1.0;

    vec3 projCoords = lightSpacePos.xyz / max(lightSpacePos.w, 0.0001);
    vec2 shadowUV = vec2(projCoords.x * 0.5 + 0.5, 0.5 - projCoords.y * 0.5);
    float currentDepth = projCoords.z * 0.5 + 0.5;

    if (shadowUV.x <= 0.001 || shadowUV.x >= 0.999 ||
        shadowUV.y <= 0.001 || shadowUV.y >= 0.999 ||
        currentDepth >= 1.0 || currentDepth <= 0.0) {
        return 1.0;
    }

    float baseBias = uPsData[17].y;
    float NdotL = clamp(dot(N, L), 0.0, 1.0);
    float bias = (baseBias > 0.00001) ? max(baseBias * (1.0 - NdotL) * 2.5, baseBias) : 0.0;
    float texelSize = max(uPsData[17].z, 1.0 / 2048.0);
    float minLit = uPsData[17].w;

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
#endif

void main() {
    vec3 N = normalize(vNormal);
    vec3 V = normalize(uPsData[0].xyz - vWorldPos);
    vec3 dirL = normalize(uPsData[1].xyz);
    bool useBlinnPhong = (uPsData[0].w > 0.5);

    vec4 texColor = texture(uTexture, vTexCoord);
    if (uPsData[3].w > 0.5 && texColor.a < 0.5) {
        discard;
    }
    bool hasTex = dot(texColor.rgb, vec3(1.0)) >= 0.01;
    vec3 baseAlbedo = clamp(hasTex ? (texColor.rgb * uPsData[3].rgb) : (vColor.rgb * uPsData[3].rgb), 0.0, 1.0);
    vec3 albedoLin = baseAlbedo * baseAlbedo;

#if USE_NORMAL_MAP
    float normalMapEnabled = uPsData[4].y;
    float bumpScale = uPsData[4].z;
    if (normalMapEnabled > 0.5 && hasTex && bumpScale > 0.01) {
        vec3 dp1 = dFdx(vWorldPos);
        vec3 dp2 = dFdy(vWorldPos);
        vec2 duv1 = dFdx(vTexCoord);
        vec2 duv2 = dFdy(vTexCoord);

        vec3 dp2perp = cross(dp2, N);
        vec3 dp1perp = cross(N, dp1);
        vec3 T = dp2perp * duv1.x + dp1perp * duv2.x;
        vec3 B = dp2perp * duv1.y + dp1perp * duv2.y;

        float invmax = inversesqrt(max(max(dot(T, T), dot(B, B)), 1e-8));
        mat3 TBN = mat3(T * invmax, B * invmax, N);

        float dUV = 0.00390625;
        float hC = dot(texColor.rgb, vec3(0.299, 0.587, 0.114));
        float hR = dot(texture(uTexture, vTexCoord + vec2(dUV, 0.0)).rgb, vec3(0.299, 0.587, 0.114));
        float hU = dot(texture(uTexture, vTexCoord + vec2(0.0, dUV)).rgb, vec3(0.299, 0.587, 0.114));

        vec3 tanPerturb = normalize(vec3((hC - hR) * bumpScale, (hC - hU) * bumpScale, 1.0));
        N = normalize(TBN * tanPerturb);
    }
#endif

#if USE_SHADOWS
    float shadowFactor = ComputeDirectionalShadow(vLightSpacePos, N, dirL);
    if (uPsData[17].x > 3.5) {
        vec3 dbgCol = mix(vec3(0.08, 0.12, 0.28), vec3(0.95, 0.96, 0.98), shadowFactor);
        fragColor = vec4(dbgCol, 1.0);
        return;
    }
    vec3 dirRad = uPsData[2].rgb * (1.25 * shadowFactor);
#else
    vec3 dirRad = uPsData[2].rgb * 1.25;
#endif

    float roughness = clamp(uPsData[5].x, 0.05, 1.0);
    float metallic = clamp(uPsData[5].y, 0.0, 1.0);
    float iblStrength = uPsData[4].x;
    float ambScale = max(uPsData[4].w, 0.08);
    vec3 hemiColor = mix(vec3(0.52, 0.56, 0.64), vec3(0.95, 0.98, 1.05), N.y * 0.5 + 0.5);

#if USE_MULTILIGHT
    int numPoints = int(uPsData[5].z + 0.5);
    float spotIntensity = uPsData[5].w;
#endif

    if (useBlinnPhong) {
        float shininess = mix(128.0, 8.0, roughness);
        vec3 specColor = mix(vec3(0.45), baseAlbedo, metallic);
        vec3 Lo = EvalBlinnPhongLight(N, V, dirL, dirRad, baseAlbedo, shininess, specColor);

#if USE_MULTILIGHT
        for (int i = 0; i < 4; ++i) {
            if (i >= numPoints) break;
            vec4 pPosRad = uPsData[6 + i];
            vec4 pColInt = uPsData[10 + i];
            vec3 toLight = pPosRad.xyz - vWorldPos;
            float distSq = dot(toLight, toLight);
            float radius = max(pPosRad.w, 0.1);
            if (distSq < radius * radius) {
                float invDist = inversesqrt(max(distSq, 1e-6));
                float dist = distSq * invDist;
                vec3 L = toLight * invDist;
                float falloff = clamp(1.0 - (dist / radius), 0.0, 1.0);
                float atten = (falloff * falloff) / (1.0 + 0.08 * distSq);
                Lo += EvalBlinnPhongLight(N, V, L, pColInt.rgb * (pColInt.w * atten), baseAlbedo, shininess, specColor);
            }
        }

        if (spotIntensity > 0.01) {
            vec3 spotPos = uPsData[14].xyz;
            float spotRange = max(uPsData[14].w, 0.1);
            vec3 spotDir = normalize(uPsData[15].xyz);
            float cosInner = uPsData[15].w;
            vec3 spotCol = uPsData[16].rgb;
            float cosOuter = uPsData[16].w;
            vec3 toSpot = spotPos - vWorldPos;
            float distSq = dot(toSpot, toSpot);
            if (distSq < spotRange * spotRange) {
                float invDist = inversesqrt(max(distSq, 1e-6));
                float dist = distSq * invDist;
                vec3 L = toSpot * invDist;
                float theta = dot(-L, spotDir);
                float coneT = clamp((theta - cosOuter) / max(cosInner - cosOuter, 0.001), 0.0, 1.0);
                float cone = coneT * coneT * (3.0 - 2.0 * coneT);
                float falloff = clamp(1.0 - (dist / spotRange), 0.0, 1.0);
                Lo += EvalBlinnPhongLight(N, V, L, spotCol * (spotIntensity * cone * falloff * falloff),
                                          baseAlbedo, shininess, specColor);
            }
        }
#endif

        vec3 color = clamp(baseAlbedo * (hemiColor * ambScale) + Lo, 0.0, 1.0);
        fragColor = vec4(color, 1.0);
        return;
    }

    // Ruta PBR Cook-Torrance pre-factorizada para AMD Latte VLIW5
    vec3 F0 = mix(vec3(0.04), albedoLin, metallic);
    float NdotV = max(dot(N, V), 0.001);
    float a = roughness * roughness;
    float a2 = a * a;
    float a2m1 = a2 - 1.0;
    float r1 = roughness + 1.0;
    float k = (r1 * r1) * 0.125;
    float inv1mK = 1.0 - k;
    float ggxV = NdotV / (NdotV * inv1mK + k);
    float inv4NdotV = 0.25 / NdotV;
    vec3 diffBase = albedoLin * ((1.0 - metallic) * (1.0 / PI));

    vec3 Lo = EvalPBRLightFast(N, V, dirL, dirRad, diffBase, F0, a2, a2m1, k, inv1mK, ggxV, inv4NdotV);

#if USE_MULTILIGHT
    for (int i = 0; i < 4; ++i) {
        if (i >= numPoints) break;
        vec4 pPosRad = uPsData[6 + i];
        vec4 pColInt = uPsData[10 + i];
        vec3 toLight = pPosRad.xyz - vWorldPos;
        float distSq = dot(toLight, toLight);
        float radius = max(pPosRad.w, 0.1);
        if (distSq < radius * radius) {
            float invDist = inversesqrt(max(distSq, 1e-6));
            float dist = distSq * invDist;
            vec3 L = toLight * invDist;
            float falloff = clamp(1.0 - (dist / radius), 0.0, 1.0);
            float atten = (falloff * falloff) / (1.0 + 0.08 * distSq);
            vec3 rad = pColInt.rgb * (pColInt.w * atten);
            Lo += EvalPBRLightFast(N, V, L, rad, diffBase, F0, a2, a2m1, k, inv1mK, ggxV, inv4NdotV);
        }
    }

    if (spotIntensity > 0.01) {
        vec3 spotPos = uPsData[14].xyz;
        float spotRange = max(uPsData[14].w, 0.1);
        vec3 spotDir = normalize(uPsData[15].xyz);
        float cosInner = uPsData[15].w;
        vec3 spotCol = uPsData[16].rgb;
        float cosOuter = uPsData[16].w;

        vec3 toSpot = spotPos - vWorldPos;
        float distSq = dot(toSpot, toSpot);
        if (distSq < spotRange * spotRange) {
            float invDist = inversesqrt(max(distSq, 1e-6));
            float dist = distSq * invDist;
            vec3 L = toSpot * invDist;
            float theta = dot(-L, spotDir);
            float coneT = clamp((theta - cosOuter) / max(cosInner - cosOuter, 0.001), 0.0, 1.0);
            float cone = coneT * coneT * (3.0 - 2.0 * coneT);
            float falloff = clamp(1.0 - (dist / spotRange), 0.0, 1.0);
            vec3 rad = spotCol * (spotIntensity * cone * falloff * falloff);
            Lo += EvalPBRLightFast(N, V, L, rad, diffBase, F0, a2, a2m1, k, inv1mK, ggxV, inv4NdotV);
        }
    }
#endif

    vec3 ambient = hemiColor * albedoLin * ambScale * (1.0 - 0.55 * metallic);
    vec3 envRefl = vec3(0.0);
    if (iblStrength > 0.01) {
        vec3 R = reflect(-V, N);
        vec3 envSpecS = clamp(SampleCubeAtlas(R), 0.0, 1.0);
        vec3 envIrrS  = clamp(SampleCubeAtlas(N), 0.0, 1.0);
        vec3 envSpec = envSpecS * envSpecS;
        vec3 envIrr  = envIrrS * envIrrS;
        vec3 F_ibl = FresnelSchlick(NdotV, F0);
        vec3 kD_ibl = (vec3(1.0) - F_ibl) * (1.0 - metallic);
        vec3 specBlur = mix(envSpec, envIrr, a);
        ambient = kD_ibl * envIrr * albedoLin * (0.45 * iblStrength) + ambient * (1.0 - 0.5 * iblStrength);
        envRefl = F_ibl * specBlur * ((1.0 - roughness * 0.72) * iblStrength * 1.35);
    }
    vec3 color = ambient + envRefl + Lo;
    color = ToneMapACES(color * 1.18);
    color = sqrt(color);

    fragColor = vec4(color, 1.0);
}

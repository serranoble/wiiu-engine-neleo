#version 330 core

in vec3 vWorldPos;
in vec3 vNormal;
in vec2 vTexCoord;
in vec4 vColor;
in vec4 vLightSpacePos;
in vec3 vLocalPos;

uniform sampler2D uTexture;
uniform sampler2D uSkybox;

// Empaquetado contiguo de 17 vec4 (68 floats) — variante ligera sin Shadow Map/PCF/Fbm3D/Agua/Vidrio/NPR
// para reducir GPRs y maximizar la ocupación de wavefronts en AMD Latte R700 (VLIW5).
// [0]  = (camPos.xyz, shadingMode: 0=PBR Cook-Torrance, 1=Blinn-Phong)
// [1]  = (dirLightDir.xyz, elapsedTime)
// [2]  = (dirLightColor.rgb, dirIntensityOrShininess)
// [3]  = (albedoTint.rgb, alphaCutoutFlag)
// [4]  = (reflectionIntensity [IBL], normalMapEnabled, bumpScale, ambientIntensity)
// [5]  = (roughness, metallic, activePointLights, spotIntensity)
// [6..9]   = pointLightPosRadius[0..3]
// [10..13] = pointLightColInt[0..3]
// [14..16] = spotLight (posRange, dirInner, colorOuter)
uniform vec4 uPsData[17];

out vec4 fragColor;

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

// Cook-Torrance GGX pre-factorizado para Latte VLIW5 (NdotV, a2, k, ggxV y diffBase calculados 1 sola vez/pixel)
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

    // Normal / Bump Mapping analítico (TBN con dFdx/dFdy)
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

    float roughness = clamp(uPsData[5].x, 0.05, 1.0);
    float metallic = clamp(uPsData[5].y, 0.0, 1.0);
    int numPoints = int(uPsData[5].z + 0.5);
    float spotIntensity = uPsData[5].w;
    float iblStrength = uPsData[4].x;
    float ambScale = max(uPsData[4].w, 0.08);
    vec3 hemiColor = mix(vec3(0.52, 0.56, 0.64), vec3(0.95, 0.98, 1.05), N.y * 0.5 + 0.5);
    vec3 dirRad = uPsData[2].rgb * 1.25;

    if (useBlinnPhong) {
        float shininess = mix(128.0, 8.0, roughness);
        vec3 specColor = mix(vec3(0.45), baseAlbedo, metallic);
        vec3 Lo = EvalBlinnPhongLight(N, V, dirL, dirRad, baseAlbedo, shininess, specColor);

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

        vec3 color = clamp(baseAlbedo * (hemiColor * ambScale) + Lo, 0.0, 1.0);
        fragColor = vec4(color, 1.0);
        return;
    }

    // Ruta PBR Cook-Torrance (precalculo 1 vez/pixel para 6 luces a 60 FPS en Latte VLIW5)
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

    // 1. Luz Direccional
    vec3 Lo = EvalPBRLightFast(N, V, dirL, dirRad, diffBase, F0, a2, a2m1, k, inv1mK, ggxV, inv4NdotV);

    // 2. Hasta 4 Luces Puntuales Dinámicas (atenuación 1:1 con PC: 0.08 * distSq)
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

    // 3. Luz Focal (Spot Light, atenuación 1:1 con PC: cone * falloff^2)
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

    // 4. Luz Ambiental Hemisférica + IBL opcional
    vec3 ambient = hemiColor * albedoLin * ambScale * (1.0 - 0.55 * metallic);
    vec3 envRefl = hemiColor * F0 * (1.0 - roughness * 0.85) * 0.25;
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

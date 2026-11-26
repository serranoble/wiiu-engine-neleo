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

float DistributionGGX(vec3 N, vec3 H, float roughness) {
    float a = roughness * roughness;
    float a2 = a * a;
    float NdotH = max(dot(N, H), 0.0);
    float NdotH2 = NdotH * NdotH;
    float denom = (NdotH2 * (a2 - 1.0) + 1.0);
    return a2 / max(PI * denom * denom, 0.0001);
}

float GeometrySchlickGGX(float NdotV, float roughness) {
    float r = roughness + 1.0;
    float k = (r * r) / 8.0;
    return NdotV / max(NdotV * (1.0 - k) + k, 0.0001);
}

float GeometrySmith(vec3 N, vec3 V, vec3 L, float roughness) {
    float NdotV = max(dot(N, V), 0.0);
    float NdotL = max(dot(N, L), 0.0);
    return GeometrySchlickGGX(NdotV, roughness) * GeometrySchlickGGX(NdotL, roughness);
}

vec3 FresnelSchlick(float cosTheta, vec3 F0) {
    return F0 + (1.0 - F0) * pow(clamp(1.0 - cosTheta, 0.0, 1.0), 5.0);
}

vec3 EvalPBRLight(vec3 N, vec3 V, vec3 L, vec3 radiance, vec3 albedo, float roughness, float metallic, vec3 F0) {
    float NdotL = max(dot(N, L), 0.0);
    if (NdotL <= 0.0) return vec3(0.0);

    vec3 H = normalize(V + L);
    float NDF = DistributionGGX(N, H, roughness);
    float G = GeometrySmith(N, V, L, roughness);
    vec3 F = FresnelSchlick(max(dot(H, V), 0.0), F0);

    vec3 specular = (NDF * G * F) / max(4.0 * max(dot(N, V), 0.0) * NdotL, 0.0001);
    vec3 kD = (vec3(1.0) - F) * (1.0 - metallic);
    return (kD * albedo / PI + specular) * radiance * NdotL;
}

vec3 EvalBlinnPhongLight(vec3 N, vec3 V, vec3 L, vec3 radiance, vec3 albedo, float roughness, float metallic) {
    float NdotL = max(dot(N, L), 0.0);
    if (NdotL <= 0.0) return vec3(0.0);

    vec3 H = normalize(V + L);
    float shininess = mix(128.0, 8.0, roughness);
    float spec = pow(max(dot(N, H), 0.0), shininess);
    vec3 specColor = mix(vec3(0.45), albedo, metallic);
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
    vec3 baseAlbedo = hasTex ? (texColor.rgb * uPsData[3].rgb) : (vColor.rgb * uPsData[3].rgb);
    vec3 albedoLin = pow(clamp(baseAlbedo, 0.0, 1.0), vec3(2.2));

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
    vec3 F0 = mix(vec3(0.04), albedoLin, metallic);

    vec3 Lo = vec3(0.0);

    // 1. Luz Direccional
    vec3 dirRad = uPsData[2].rgb * 1.45;
    if (useBlinnPhong) {
        Lo += EvalBlinnPhongLight(N, V, dirL, dirRad, baseAlbedo, roughness, metallic);
    } else {
        Lo += EvalPBRLight(N, V, dirL, dirRad, albedoLin, roughness, metallic, F0);
    }

    // 2. Hasta 4 Luces Puntuales Dinámicas
    int numPoints = int(uPsData[5].z + 0.5);
    for (int i = 0; i < 4; ++i) {
        if (i >= numPoints) break;
        vec4 pPosRad = uPsData[6 + i];
        vec4 pColInt = uPsData[10 + i];
        vec3 toLight = pPosRad.xyz - vWorldPos;
        float dist = length(toLight);
        float radius = max(pPosRad.w, 0.1);
        if (dist < radius) {
            vec3 L = toLight / max(dist, 0.0001);
            float falloff = clamp(1.0 - (dist / radius), 0.0, 1.0);
            float atten = (falloff * falloff) / (1.0 + 0.18 * dist * dist);
            vec3 rad = pColInt.rgb * (pColInt.w * atten);
            if (useBlinnPhong) {
                Lo += EvalBlinnPhongLight(N, V, L, rad, baseAlbedo, roughness, metallic);
            } else {
                Lo += EvalPBRLight(N, V, L, rad, albedoLin, roughness, metallic, F0);
            }
        }
    }

    // 3. Luz Focal (Spot Light)
    float spotIntensity = uPsData[5].w;
    if (spotIntensity > 0.01) {
        vec3 spotPos = uPsData[14].xyz;
        float spotRange = max(uPsData[14].w, 0.1);
        vec3 spotDir = normalize(uPsData[15].xyz);
        float cosInner = uPsData[15].w;
        vec3 spotCol = uPsData[16].rgb;
        float cosOuter = uPsData[16].w;

        vec3 toSpot = spotPos - vWorldPos;
        float dist = length(toSpot);
        if (dist < spotRange) {
            vec3 L = toSpot / max(dist, 0.0001);
            float theta = dot(-L, spotDir);
            float eps = max(cosInner - cosOuter, 0.001);
            float cone = clamp((theta - cosOuter) / eps, 0.0, 1.0);
            float falloff = clamp(1.0 - (dist / spotRange), 0.0, 1.0);
            float atten = (falloff * falloff) * cone / (1.0 + 0.06 * dist * dist);
            vec3 rad = spotCol * (spotIntensity * atten);
            if (useBlinnPhong) {
                Lo += EvalBlinnPhongLight(N, V, L, rad, baseAlbedo, roughness, metallic);
            } else {
                Lo += EvalPBRLight(N, V, L, rad, albedoLin, roughness, metallic, F0);
            }
        }
    }

    // 4. Luz Ambiental Hemisférica + IBL opcional
    float iblStrength = uPsData[4].x;
    float ambScale = uPsData[4].w;
    vec3 hemiColor = mix(vec3(0.08, 0.09, 0.12), vec3(0.24, 0.28, 0.36), N.y * 0.5 + 0.5);
    vec3 color;
    if (useBlinnPhong) {
        color = baseAlbedo * (hemiColor * ambScale * 1.4) + Lo;
        color = clamp(color, 0.0, 1.0);
    } else {
        vec3 ambient = hemiColor * albedoLin * ambScale * (1.0 - 0.55 * metallic);
        vec3 envRefl = hemiColor * F0 * (1.0 - roughness * 0.85) * 0.9;
        if (iblStrength > 0.01) {
            vec3 R = reflect(-V, N);
            vec3 envSpec = pow(clamp(SampleCubeAtlas(R), 0.0, 1.0), vec3(2.2));
            vec3 envIrr  = pow(clamp(SampleCubeAtlas(N), 0.0, 1.0), vec3(2.2));
            float NdotV = clamp(dot(N, V), 0.0, 1.0);
            vec3 F_ibl = FresnelSchlick(NdotV, F0);
            vec3 kD_ibl = (vec3(1.0) - F_ibl) * (1.0 - metallic);
            vec3 specBlur = mix(envSpec, envIrr, roughness * roughness);
            ambient = kD_ibl * envIrr * albedoLin * (0.45 * iblStrength) + ambient * (1.0 - 0.5 * iblStrength);
            envRefl = F_ibl * specBlur * ((1.0 - roughness * 0.72) * iblStrength * 1.35);
        }
        color = ambient + envRefl + Lo;
        color = color / (color + vec3(1.0));
        color = pow(clamp(color, 0.0, 1.0), vec3(1.0 / 2.2));
    }

    fragColor = vec4(color, 1.0);
}

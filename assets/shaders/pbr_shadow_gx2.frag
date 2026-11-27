#version 330 core

in vec3 vWorldPos;
in vec3 vNormal;
in vec2 vTexCoord;
in vec4 vColor;
in vec4 vLightSpacePos;
in vec3 vLocalPos;

uniform sampler2D uTexture;
uniform sampler2D uSkybox;
uniform sampler2D uShadowMap;

// Variante especializada PBR + Shadow Mapping PCF (Suite G3) — sin Fbm3D, agua, vidrio, terreno ni NPR
uniform vec4 uPsData[18];

out vec4 fragColor;

const float PI = 3.14159265359;

vec3 SampleCubeAtlas(vec3 dir) {
    vec3 a = abs(dir);
    float sc = 0.0, tc = 0.0, col = 0.0, row = 0.0;
    if (a.x >= a.y && a.x >= a.z) {
        float invM = 1.0 / max(a.x, 0.00001);
        if (dir.x >= 0.0) { sc = -dir.z * invM; tc = -dir.y * invM; col = 0.0; row = 0.0; }
        else              { sc =  dir.z * invM; tc = -dir.y * invM; col = 1.0; row = 0.0; }
    } else if (a.y >= a.x && a.y >= a.z) {
        float invM = 1.0 / max(a.y, 0.00001);
        if (dir.y >= 0.0) { sc =  dir.x * invM; tc =  dir.z * invM; col = 2.0; row = 0.0; }
        else              { sc =  dir.x * invM; tc = -dir.z * invM; col = 0.0; row = 1.0; }
    } else {
        float invM = 1.0 / max(a.z, 0.00001);
        if (dir.z >= 0.0) { sc =  dir.x * invM; tc = -dir.y * invM; col = 1.0; row = 1.0; }
        else              { sc = -dir.x * invM; tc = -dir.y * invM; col = 2.0; row = 1.0; }
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
    float f = clamp(1.0 - cosTheta, 0.0, 1.0);
    float f2 = f * f;
    return F0 + (1.0 - F0) * (f2 * f2 * f);
}

vec3 ToneMapACES(vec3 x) {
    return clamp((x * (2.51 * x + 0.03)) / (x * (2.43 * x + 0.59) + 0.14), 0.0, 1.0);
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
    float texelSize = uPsData[17].z;
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

    // Normal / Bump Mapping analítico (TBN con dFdx/dFdy) antes de ComputeDirectionalShadow
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
    float shadowFactor = ComputeDirectionalShadow(vLightSpacePos, N, dirL);

    if (uPsData[17].x > 3.5) {
        vec3 dbgCol = mix(vec3(0.08, 0.12, 0.28), vec3(0.95, 0.96, 0.98), shadowFactor);
        fragColor = vec4(dbgCol, 1.0);
        return;
    }

    vec3 F0 = mix(vec3(0.04), albedoLin, metallic);
    vec3 Lo = vec3(0.0);

    vec3 dirRad = uPsData[2].rgb * (1.45 * shadowFactor);
    if (useBlinnPhong) {
        Lo += EvalBlinnPhongLight(N, V, dirL, dirRad, baseAlbedo, roughness, metallic);
    } else {
        Lo += EvalPBRLight(N, V, dirL, dirRad, albedoLin, roughness, metallic, F0);
    }

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
            float atten = (falloff * falloff) / (1.0 + 0.08 * dist * dist);
            vec3 rad = pColInt.rgb * (pColInt.w * atten);
            if (useBlinnPhong) {
                Lo += EvalBlinnPhongLight(N, V, L, rad, baseAlbedo, roughness, metallic);
            } else {
                Lo += EvalPBRLight(N, V, L, rad, albedoLin, roughness, metallic, F0);
            }
        }
    }

    float iblStrength = uPsData[4].x;
    float ambScale = max(uPsData[4].w, 0.08);
    vec3 hemiColor = mix(vec3(0.52, 0.56, 0.64), vec3(0.95, 0.98, 1.05), N.y * 0.5 + 0.5);
    vec3 color;
    if (useBlinnPhong) {
        color = clamp(baseAlbedo * (hemiColor * ambScale) + Lo, 0.0, 1.0);
    } else {
        vec3 ambient = hemiColor * albedoLin * ambScale * (1.0 - 0.55 * metallic);
        vec3 envRefl = hemiColor * F0 * (1.0 - roughness * 0.85) * 0.25;
        if (iblStrength > 0.01) {
            vec3 R = reflect(-V, N);
            vec3 envSpecS = clamp(SampleCubeAtlas(R), 0.0, 1.0);
            vec3 envIrrS  = clamp(SampleCubeAtlas(N), 0.0, 1.0);
            vec3 envSpec = envSpecS * envSpecS;
            vec3 envIrr  = envIrrS * envIrrS;
            float NdotV = clamp(dot(N, V), 0.0, 1.0);
            vec3 F_ibl = FresnelSchlick(NdotV, F0);
            vec3 kD_ibl = (vec3(1.0) - F_ibl) * (1.0 - metallic);
            vec3 specBlur = mix(envSpec, envIrr, roughness * roughness);
            ambient = kD_ibl * envIrr * albedoLin * (0.45 * iblStrength) + ambient * (1.0 - 0.5 * iblStrength);
            envRefl = F_ibl * specBlur * ((1.0 - roughness * 0.72) * iblStrength * 1.35);
        }
        color = ambient + envRefl + Lo;
        color = ToneMapACES(color * 1.18);
        color = sqrt(color);
    }

    fragColor = vec4(color, 1.0);
}

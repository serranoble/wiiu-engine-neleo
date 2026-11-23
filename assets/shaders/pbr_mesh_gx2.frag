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

// Empaquetado contiguo de 18 vec4 (72 floats) para garantizar offsets exactos en CafeGLSL register mode:
// [0]  = (camPos.xyz, shadingMode: 0=PBR, 1=BlinnPhong, 2=DebugNormals, 3=DebugRoughMetal, 4=GlassRefract, 5=Water, 6=SkyboxCube)
// [1]  = (dirLightDir.xyz, elapsedTime)
// [2]  = (dirLightColor.rgb, dirIntensityOrShininess)
// [3]  = (albedoTint.rgb, customParam: ior para vidrio / waveScale para agua)
// [4]  = (reflectionIntensity [IBL], normalMapEnabled, bumpScale, ambientIntensity)
// [5]  = (roughness, metallic, activePointLights, spotIntensity)
// [6..9]   = pointLightPosRadius[0..3]  (xyz=pos, w=radius)
// [10..13] = pointLightColInt[0..3]     (rgb=color, w=intensity)
// [14] = (spotPos.xyz, spotRange)
// [15] = (spotDir.xyz, cosInnerCutoff)
// [16] = (spotColor.rgb, cosOuterCutoff)
// [17] = (shadowMode: 0=OFF, 1=Hard, 2=PCF3x3, 3=PCF5x5, 4=DebugShadow; depthBias; texelSize; shadowMinLit)
uniform vec4 uPsData[18];

out vec4 fragColor;

const float PI = 3.14159265359;

// Ruido de valor 3D analítico branchless (3 octavas fBm sin recursión para AMD Latte VLIW5)
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

// Decodifica un vector de direccion 3D sobre un Atlas 2D 3x2 de 6 caras (+X,-X,+Y / -Y,+Z,-Z).
// Evita la instruccion R600 CUBE de CafeGLSL (incompatible con Cemu 2.6 Vulkan) y es 100% exacto en Latte HW.
vec3 SampleCubeAtlas(vec3 dir) {
    vec3 a = abs(dir);
    float sc = 0.0;
    float tc = 0.0;
    float col = 0.0;
    float row = 0.0;

    if (a.x >= a.y && a.x >= a.z) {
        float invM = 1.0 / max(a.x, 0.00001);
        if (dir.x >= 0.0) {
            // Face 0: +X (col 0, row 0)
            sc = -dir.z * invM;
            tc = -dir.y * invM;
            col = 0.0; row = 0.0;
        } else {
            // Face 1: -X (col 1, row 0)
            sc =  dir.z * invM;
            tc = -dir.y * invM;
            col = 1.0; row = 0.0;
        }
    } else if (a.y >= a.x && a.y >= a.z) {
        float invM = 1.0 / max(a.y, 0.00001);
        if (dir.y >= 0.0) {
            // Face 2: +Y (col 2, row 0)
            sc =  dir.x * invM;
            tc =  dir.z * invM;
            col = 2.0; row = 0.0;
        } else {
            // Face 3: -Y (col 0, row 1)
            sc =  dir.x * invM;
            tc = -dir.z * invM;
            col = 0.0; row = 1.0;
        }
    } else {
        float invM = 1.0 / max(a.z, 0.00001);
        if (dir.z >= 0.0) {
            // Face 4: +Z (col 1, row 1)
            sc =  dir.x * invM;
            tc = -dir.y * invM;
            col = 1.0; row = 1.0;
        } else {
            // Face 5: -Z (col 2, row 1)
            sc = -dir.x * invM;
            tc = -dir.y * invM;
            col = 2.0; row = 1.0;
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
    float shadingMode = uPsData[0].w;

    // Modo 9: Partículas Billboard Suaves (uPsData[3].w >= 0.5) o Toon / Cel Shading NPR con Outline (RTR4 Cap. 13 & 15)
    if (shadingMode >= 8.5) {
        if (uPsData[3].w >= 0.5) {
            vec2 c = vTexCoord * 2.0 - vec2(1.0);
            float r2 = dot(c, c);
            if (r2 > 1.0) {
                discard;
            }
            float mask = pow(clamp(1.0 - r2, 0.0, 1.0), 1.8);
            vec3 pCol = uPsData[3].rgb * (0.75 + 0.85 * mask);
            fragColor = vec4(clamp(pCol, 0.0, 1.0), clamp(mask * uPsData[4].w, 0.0, 1.0));
            return;
        }

        vec3 camPos9 = uPsData[0].xyz;
        vec3 V9 = normalize(camPos9 - vWorldPos);
        vec3 N9 = normalize(vNormal);
        vec3 L9 = normalize(uPsData[1].xyz);
        float NdotV9 = clamp(dot(N9, V9), 0.0, 1.0);

        // Outline oscuro de silueta (activable con uPsData[4].y > 0.5)
        if (uPsData[4].y > 0.5 && NdotV9 < 0.24) {
            fragColor = vec4(0.04, 0.05, 0.08, 1.0);
            return;
        }

        float NdotL9 = max(dot(N9, L9), 0.0);
        float toonDiff = (floor(NdotL9 * 4.0) + 0.35) / 4.0;
        vec3 H9 = normalize(V9 + L9);
        float toonSpec = step(0.92, max(dot(N9, H9), 0.0)) * 0.65;
        float rim = smoothstep(0.62, 0.92, 1.0 - NdotV9) * 0.32;

        vec3 base9 = vColor.rgb * uPsData[3].rgb;
        vec3 col9 = base9 * (0.24 + toonDiff * 0.92) * uPsData[2].rgb + vec3(toonSpec + rim);
        fragColor = vec4(clamp(col9, 0.0, 1.0), 1.0);
        return;
    }

    // Modo 8: Terreno Heightmap Splatting (sub=0) y Ruido 3D Procedural Mármol (sub=1) / Madera (sub=2) (CGP Cap. 12 & 14)
    if (shadingMode >= 7.5) {
        vec3 camPos8 = uPsData[0].xyz;
        vec3 V8 = normalize(camPos8 - vWorldPos);
        vec3 N8 = normalize(vNormal);
        vec3 L8 = normalize(uPsData[1].xyz);
        float subMode = uPsData[3].w;

        vec3 albedo8 = vec3(0.5);
        if (subMode < 0.5) {
            // Splatting multitextura por altura (vColor.r) y pendiente (vColor.g) + micro-ruido 3D
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
            // Mármol 3D Procedural (CGP Cap. 14)
            vec3 p3 = vLocalPos * 2.4;
            float turb = Fbm3D(p3);
            float vein = 0.5 + 0.5 * sin(p3.x * 3.8 + p3.y * 1.4 + turb * 6.2);
            vein = pow(clamp(vein, 0.0, 1.0), 0.55);
            albedo8 = mix(vec3(0.16, 0.20, 0.26), vec3(0.95, 0.94, 0.90), vein);
        } else {
            // Madera 3D Procedural (anillos concéntricos + turbulencia 3D)
            vec3 p3 = vLocalPos * 2.2;
            float turb = Fbm3D(p3 * 1.35);
            float ringDist = length(p3.xz * vec2(1.0, 1.3)) * 4.2 + turb * 0.95;
            float ring = 0.5 + 0.5 * sin(ringDist * 6.283185);
            albedo8 = mix(vec3(0.30, 0.16, 0.07), vec3(0.68, 0.44, 0.22), smoothstep(0.15, 0.85, ring));
        }

        float NdotV8 = clamp(dot(N8, V8), 0.0, 1.0);
        bool toonOn = (uPsData[4].x > 0.5);
        if (toonOn && uPsData[4].y > 0.5 && subMode >= 0.5 && NdotV8 < 0.22) {
            fragColor = vec4(0.04, 0.05, 0.08, 1.0);
            return;
        }

        float NdotL8 = max(dot(N8, L8), 0.0);
        float diff8 = toonOn ? ((floor(NdotL8 * 4.0) + 0.35) / 4.0) : NdotL8;
        vec3 H8 = normalize(V8 + L8);
        float spec8 = toonOn ? (step(0.92, max(dot(N8, H8), 0.0)) * 0.45)
                             : (pow(max(dot(N8, H8), 0.0), 36.0) * 0.28);

        vec3 hemi8 = mix(vec3(0.12, 0.14, 0.18), vec3(0.30, 0.38, 0.50), N8.y * 0.5 + 0.5);
        vec3 col8 = albedo8 * (hemi8 + uPsData[2].rgb * (diff8 * 1.10)) + uPsData[2].rgb * spec8;

        // Luz puntual cálida de la hoguera (slot 0)
        if (uPsData[5].z > 0.5) {
            vec4 pPR = uPsData[6];
            vec4 pCI = uPsData[10];
            vec3 toL = pPR.xyz - vWorldPos;
            float d = length(toL);
            float rad = max(pPR.w, 0.1);
            if (d < rad) {
                vec3 Lp = toL / max(d, 0.0001);
                float ff = clamp(1.0 - d / rad, 0.0, 1.0);
                float att = (ff * ff) / (1.0 + 0.20 * d * d);
                float ndl = max(dot(N8, Lp), 0.0);
                col8 += albedo8 * pCI.rgb * (pCI.w * att * (toonOn ? (floor(ndl * 3.0 + 0.5) / 3.0) : ndl));
            }
        }

        fragColor = vec4(clamp(col8, 0.0, 1.0), 1.0);
        return;
    }

    // Modo 7: Escena HDR Lineal (Emisivos > 1.0, Alpha-Cutout discard, Vitrales AlphaBlend y Niebla CGP Cap. 14)
    // Salida en espacio lineal HDR sin tone mapping interno (delegado a PostProcessHDRGX2).
    if (shadingMode >= 6.5) {
        vec4 texCol = texture(uTexture, vTexCoord);
        float alphaCutout = uPsData[4].x;
        if (alphaCutout > 0.5 && texCol.a < 0.5) {
            discard;
        }
        bool hasTex = dot(texCol.rgb, vec3(1.0)) >= 0.01;
        vec3 baseCol = hasTex ? (texCol.rgb * uPsData[3].rgb) : (vColor.rgb * uPsData[3].rgb);
        vec3 albedoLin = pow(clamp(baseCol, 0.0, 1.0), vec3(2.2));

        vec3 camPos7 = uPsData[0].xyz;
        vec3 V7 = normalize(camPos7 - vWorldPos);
        vec3 N7 = normalize(vNormal);
        vec3 L7 = normalize(uPsData[1].xyz);
        float emissiveBoost = uPsData[3].w;

        vec3 hdrCol;
        if (emissiveBoost > 0.05) {
            // Superficie emisiva HDR (> 1.0) con leve refuerzo central Fresnel para disparar Bloom
            float core = 0.65 + 0.35 * clamp(abs(dot(N7, V7)), 0.0, 1.0);
            hdrCol = albedoLin * (emissiveBoost * core);
        } else {
            float NdotL = max(dot(N7, L7), 0.0);
            vec3 H7 = normalize(V7 + L7);
            float roughness7 = clamp(uPsData[5].x, 0.08, 1.0);
            float specPow = mix(96.0, 12.0, roughness7);
            float spec = pow(max(dot(N7, H7), 0.0), specPow) * (1.0 - roughness7 * 0.7);
            vec3 hemi = mix(vec3(0.03, 0.04, 0.07), vec3(0.10, 0.13, 0.20), N7.y * 0.5 + 0.5);
            hdrCol = albedoLin * (hemi + uPsData[2].rgb * (NdotL * 1.15)) + uPsData[2].rgb * (spec * 0.55);

            // Contribución de las 4 luces puntuales (orbes emisivos iluminando suelo, pilares y vitrales)
            int numPts7 = int(uPsData[5].z + 0.5);
            for (int i = 0; i < 4; ++i) {
                if (i >= numPts7) break;
                vec4 pPR = uPsData[6 + i];
                vec4 pCI = uPsData[10 + i];
                vec3 toL = pPR.xyz - vWorldPos;
                float d = length(toL);
                float rad = max(pPR.w, 0.1);
                if (d < rad) {
                    vec3 Lp = toL / max(d, 0.0001);
                    float ff = clamp(1.0 - d / rad, 0.0, 1.0);
                    float att = (ff * ff) / (1.0 + 0.22 * d * d);
                    float ndl = max(dot(N7, Lp), 0.0);
                    hdrCol += albedoLin * pCI.rgb * (pCI.w * att * ndl);
                }
            }
        }

        // Niebla atmosférica HDR (CGP Cap. 14: 0=OFF, 1=Lineal, 2=Exp, 3=Exp2)
        float fogMode = uPsData[4].y;
        if (fogMode > 0.5) {
            float dist = length(camPos7 - vWorldPos);
            float density = max(uPsData[4].z, 0.001);
            float fogFactor = 1.0;
            if (fogMode < 1.5) {
                fogFactor = clamp((38.0 - dist) / (38.0 - 6.0), 0.0, 1.0);
            } else if (fogMode < 2.5) {
                fogFactor = exp(-dist * density);
            } else {
                float f = dist * density;
                fogFactor = exp(-(f * f));
            }
            vec3 fogColorLin = vec3(0.035, 0.055, 0.095);
            hdrCol = mix(fogColorLin, hdrCol, clamp(fogFactor, 0.0, 1.0));
        }

        float alphaOut = clamp(uPsData[4].w, 0.05, 1.0);
        fragColor = vec4(max(hdrCol, vec3(0.0)), alphaOut);
        return;
    }

    // Modo 6: Skybox CubeMap infinito (CGP Cap. 9 / RTR4 Cap. 13)
    if (shadingMode >= 5.5) {
        vec3 skyDir = normalize(vLocalPos);
        fragColor = vec4(SampleCubeAtlas(skyDir), 1.0);
        return;
    }

    vec3 camPos = uPsData[0].xyz;
    vec3 V = normalize(camPos - vWorldPos);
    vec3 N = normalize(vNormal);
    vec3 dirL = normalize(uPsData[1].xyz);

    // Modo 5: Agua Física Realista (CGP Cap. 15):
    // Normales multi-escala (olas + micro-ripples), Reflexión Planar RTT (uTexture),
    // Refracción submarina + Cáusticas + Beer-Lambert, Fresnel F0=0.02, SSS, Glitter y Horizon Fade
    if (shadingMode >= 4.5) {
        float t = uPsData[1].w;
        float amp = max(uPsData[3].w, 0.05);
        vec2 p = vWorldPos.xz;
        float distCam = length(camPos - vWorldPos);
        float detailFade = clamp(1.0 - distCam / 60.0, 0.12, 1.0);

        // 1. Olas principales (swells suaves de baja frecuencia)
        float w1 = p.x * 0.48 + p.y * 0.28 + t * 1.45;
        float w2 = p.x * -0.36 + p.y * 0.62 + t * 1.15;
        float w3 = p.x * 0.85 - p.y * 0.72 + t * 1.95;

        float dhdx = (cos(w1) * 0.48 * 0.055 + cos(w2) * -0.36 * 0.048 + cos(w3) * 0.85 * 0.026) * amp;
        float dhdz = (cos(w1) * 0.28 * 0.055 + cos(w2) *  0.62 * 0.048 + cos(w3) * -0.72 * 0.026) * amp;
        float crest = sin(w1) * 0.45 + sin(w2) * 0.35 + sin(w3) * 0.20;

        // 2. Micro-ondas capilares de viento (2 capas cruzadas de alta frecuencia, atenuadas por distancia)
        float r1 = p.x * 2.35 + p.y * 1.75 + t * 2.90;
        float r2 = p.x * -2.85 + p.y * 2.40 - t * 2.45;
        float r3 = p.x * 4.60 - p.y * 3.85 + t * 3.70;
        float r4 = p.x * -5.10 - p.y * 4.30 - t * 3.25;

        float ripScale = amp * detailFade;
        dhdx += (cos(r1) * 2.35 * 0.015 + cos(r2) * -2.85 * 0.013 + cos(r3) * 4.60 * 0.007 + cos(r4) * -5.10 * 0.006) * ripScale;
        dhdz += (cos(r1) * 1.75 * 0.015 + cos(r2) *  2.40 * 0.013 + cos(r3) * -3.85 * 0.007 + cos(r4) * -4.30 * 0.006) * ripScale;

        vec3 Nw = normalize(vec3(-dhdx, 1.0, -dhdz));
        float NdotV = clamp(dot(Nw, V), 0.0, 1.0);

        // 3. Reflexión Planar Real (uTexture = RenderTargetGX2) + fallback suave al Sky Atlas
        vec3 R = reflect(-V, Nw);
        vec3 skyRefl = SampleCubeAtlas(vec3(R.x, max(R.y, 0.01), R.z));
        vec3 reflCol = skyRefl;
        if (uPsData[4].x > 0.5) {
            vec2 reflNDC = vLightSpacePos.xy / max(vLightSpacePos.w, 0.0001);
            vec2 baseReflUV = vec2(reflNDC.x * 0.5 + 0.5, 0.5 - reflNDC.y * 0.5);
            vec2 distUV = baseReflUV + Nw.xz * (0.068 * detailFade);
            vec2 clampedUV = clamp(distUV, 0.004, 0.996);
            vec3 planarCol = texture(uTexture, clampedUV).rgb;

            // Fundido suave si la coordenada se acerca al borde del Render Target
            vec2 edgeDist = min(clampedUV, 1.0 - clampedUV);
            float edgeMask = smoothstep(0.004, 0.055, min(edgeDist.x, edgeDist.y));
            reflCol = mix(skyRefl, planarCol, edgeMask);
        }

        // 4. Refracción física (IOR 1.333) + Fondo arenoso + Cáusticas duales + Absorción Beer-Lambert
        vec3 refrDir = refract(-V, Nw, 0.75);
        float radialDist = length(p - vec2(1.0, -1.8));
        float waterDepth = clamp(1.35 + radialDist * 0.14, 1.35, 5.20);
        float tBed = waterDepth / max(-refrDir.y, 0.18);
        vec2 bedXZ = p + refrDir.xz * tBed;

        // Relieve suave de arena en el fondo submarino
        float sandWave = 0.5 + 0.5 * sin(bedXZ.x * 0.9 + sin(bedXZ.y * 0.55) * 1.4);
        vec3 seabedCol = mix(vec3(0.42, 0.39, 0.30), vec3(0.62, 0.56, 0.42), sandWave);

        // Cáusticas solares duales animadas sobre el lecho
        float cA = sin(bedXZ.x * 2.1 + bedXZ.y * 1.6 + t * 1.8) * cos(bedXZ.x * 1.5 - bedXZ.y * 2.3 - t * 1.4);
        float cB = sin(bedXZ.x * 3.1 - bedXZ.y * 2.4 - t * 2.2) * cos(bedXZ.x * 2.2 + bedXZ.y * 3.0 + t * 1.7);
        float caustics = pow(clamp(0.5 + 0.5 * (cA + cB), 0.0, 1.0), 4.0) * exp(-waterDepth * 0.32) * detailFade;
        seabedCol += uPsData[2].rgb * (caustics * 0.72);

        // Absorción espectral Beer-Lambert (absorbe rojo rápido -> turquesa somero -> azul profundo)
        vec3 transmittance = exp(-tBed * vec3(0.44, 0.14, 0.065));
        vec3 deepWaterCol = vec3(0.012, 0.075, 0.145);
        vec3 underwaterCol = mix(deepWaterCol, seabedCol, transmittance);

        // 5. Fresnel físico de Schlick para agua (F0 = 0.02)
        float fresnel = 0.02 + 0.98 * pow(1.0 - NdotV, 5.0);
        vec3 col = mix(underwaterCol, reflCol, clamp(fresnel, 0.0, 1.0));

        // 6. Subsurface Scattering (SSS) turquesa en crestas a contraluz
        float sss = pow(clamp(dot(V, -dirL + Nw * 0.55), 0.0, 1.0), 3.0) * clamp(crest * 0.5 + 0.5, 0.0, 1.0);
        col += vec3(0.03, 0.34, 0.38) * (sss * 0.42 * detailFade);

        // 7. Destello solar fino (Sun Glitter) + brillo suave direccional
        vec3 H = normalize(V + dirL);
        float NdotH = max(dot(Nw, H), 0.0);
        float glitter = pow(NdotH, 384.0) * 3.4 * detailFade + pow(NdotH, 56.0) * 0.14;
        col += uPsData[2].rgb * glitter;

        // 8. Fusión atmosférica suave hacia el horizonte (oculta el borde cuadrado del plano)
        float horizonFade = smoothstep(32.0, 66.0, distCam);
        vec3 horizonSky = SampleCubeAtlas(normalize(vec3(-V.x, max(-V.y, 0.015), -V.z)));
        col = mix(col, horizonSky, horizonFade);

        fragColor = vec4(clamp(col, 0.0, 1.0), 1.0);
        return;
    }

    // Modo 4: Vidrio / Cristal Refractivo + Reflexión Fresnel (CGP Cap. 9)
    if (shadingMode >= 3.5) {
        float ior = max(uPsData[3].w, 1.01);
        float ratio = 1.0 / ior;
        vec3 R = reflect(-V, N);
        vec3 T = refract(-V, N, ratio);
        if (dot(T, T) < 0.0001) {
            T = R; // Reflexión interna total
        }

        float f0Scalar = (ior - 1.0) / (ior + 1.0);
        f0Scalar = f0Scalar * f0Scalar;
        float NdotV = clamp(dot(N, V), 0.0, 1.0);
        float fresnel = f0Scalar + (1.0 - f0Scalar) * pow(1.0 - NdotV, 5.0);

        vec3 reflCol = SampleCubeAtlas(R);
        vec3 refrCol = SampleCubeAtlas(T) * uPsData[3].rgb;
        vec3 col = mix(refrCol, reflCol, clamp(fresnel, 0.0, 1.0));

        vec3 H = normalize(V + dirL);
        float spec = pow(max(dot(N, H), 0.0), 128.0) * 1.2;
        col += uPsData[2].rgb * spec;

        fragColor = vec4(clamp(col, 0.0, 1.0), 1.0);
        return;
    }

    vec4 texColor = texture(uTexture, vTexCoord);
    bool hasTex = dot(texColor.rgb, vec3(1.0)) >= 0.01;
    vec3 baseAlbedo = hasTex ? (texColor.rgb * uPsData[3].rgb) : (vColor.rgb * uPsData[3].rgb);
    vec3 albedoLin = pow(clamp(baseAlbedo, 0.0, 1.0), vec3(2.2));

    // Normal / Bump Mapping en espacio tangente (TBN analítico con dFdx/dFdy)
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

    // Modo 2: Debug Normales en Mundo (TBN)
    if (shadingMode > 1.5 && shadingMode < 2.5) {
        fragColor = vec4(N * 0.5 + 0.5, 1.0);
        return;
    }
    // Modo 3: Debug Roughness (R) / Metallic (G)
    if (shadingMode >= 2.5) {
        fragColor = vec4(roughness, metallic, 0.15, 1.0);
        return;
    }

    float shadowFactor = ComputeDirectionalShadow(vLightSpacePos, N, dirL);

    // Modo Shadow 4: Debug visual del factor de sombra
    if (uPsData[17].x > 3.5) {
        vec3 dbgCol = mix(vec3(0.08, 0.12, 0.28), vec3(0.95, 0.96, 0.98), shadowFactor);
        fragColor = vec4(dbgCol, 1.0);
        return;
    }

    bool useBlinnPhong = (shadingMode > 0.5);
    vec3 F0 = mix(vec3(0.04), albedoLin, metallic);

    vec3 Lo = vec3(0.0);

    // 1. Luz Direccional modulada por Shadow Map
    vec3 dirRad = uPsData[2].rgb * (1.45 * shadowFactor);
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

    // 3. Luz Focal / Linterna (Spot Light)
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

    // 4. Luz Ambiental Hemisférica + IBL real desde CubeMap (RTR4 Cap. 10)
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

#version 330 core

// ============================================================================
// [OPT-458] Shader Especializado Modo 7 — Escena HDR Lineal + Niebla + Cutout
// (Suite G5: 3 Orbes HDR, 4 Celosias Alpha-Cutout, 3 Vitrales AlphaBlend, Niebla)
// Sin PBR completo, sin Fbm3D, sin Agua/Vidrio ni Shadow Map para minimizar
// PS_GPR y maximizar ocupacion de wavefronts en AMD Latte R700 (60 FPS en 720p RGBA16F).
// ============================================================================

// Sin layout(location=...) para coincidir con pbr_mesh_gx2.vert (#version 330 core)
// y garantizar enlace por nombre en CafeGLSL (SPI_PS_INPUT_CNTL).
in vec3 vWorldPos;
in vec3 vNormal;
in vec2 vTexCoord;
in vec4 vColor;
in vec4 vLightSpacePos;
in vec3 vLocalPos;

uniform sampler2D uTexture;

// Empaquetado contiguo de 14 vec4 (56 floats):
// [0]     = (camPos.xyz, shadingMode=7)
// [1]     = (dirLightDir.xyz, elapsedTime)
// [2]     = (dirLightColor.rgb, dirIntensityOrShininess)
// [3]     = (albedoTint.rgb, emissiveBoost)
// [4]     = (alphaCutout, fogMode, fogDensity, alphaOut)
// [5]     = (roughness, metallic, activePointLights, spotIntensity)
// [6..9]  = pointLightPosRadius[0..3]
// [10..13]= pointLightColInt[0..3]
uniform vec4 uPsData[14];

out vec4 fragColor;

void main() {
    vec4 texCol = texture(uTexture, vTexCoord);
    float alphaCutout = uPsData[4].x;
    if (alphaCutout > 0.5 && texCol.a < 0.5) {
        discard;
    }

    bool hasTex = dot(texCol.rgb, vec3(1.0)) >= 0.01;
    vec3 baseCol = clamp(hasTex ? (texCol.rgb * uPsData[3].rgb) : (vColor.rgb * uPsData[3].rgb), 0.0, 1.0);
    vec3 albedoLin = baseCol * baseCol;

    vec3 camPos7 = uPsData[0].xyz;
    vec3 toCam = camPos7 - vWorldPos;
    float camDistSq = dot(toCam, toCam);
    float invCamDist = inversesqrt(max(camDistSq, 1e-6));
    vec3 V7 = toCam * invCamDist;
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

        // Contribucion de hasta 4 luces puntuales (orbes emisivos) sin length() ni divisiones lentas
        int numPts7 = int(uPsData[5].z + 0.5);
        for (int i = 0; i < 4; ++i) {
            if (i >= numPts7) break;
            vec4 pPR = uPsData[6 + i];
            vec4 pCI = uPsData[10 + i];
            vec3 toL = pPR.xyz - vWorldPos;
            float distSq = dot(toL, toL);
            float rad = max(pPR.w, 0.1);
            if (distSq < rad * rad) {
                float invD = inversesqrt(max(distSq, 1e-6));
                float d = distSq * invD;
                vec3 Lp = toL * invD;
                float ff = clamp(1.0 - d / rad, 0.0, 1.0);
                float att = (ff * ff) / (1.0 + 0.22 * distSq);
                float ndl = max(dot(N7, Lp), 0.0);
                hdrCol += albedoLin * pCI.rgb * (pCI.w * att * ndl);
            }
        }
    }

    // Niebla atmosferica HDR (CGP Cap. 14: 0=OFF, 1=Lineal, 2=Exp, 3=Exp2)
    float fogMode = uPsData[4].y;
    if (fogMode > 0.5) {
        float dist = camDistSq * invCamDist;
        float density = max(uPsData[4].z, 0.001);
        float fogFactor = 1.0;
        if (fogMode < 1.5) {
            fogFactor = clamp((38.0 - dist) * (1.0 / 32.0), 0.0, 1.0);
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
}

#version 330 core

// ============================================================================
// [OPT-467] Shader Especializado Modo 8 — Terreno Splatting + Marmol/Madera 3D + Toon
// (Suite G7: extraido del uber-shader pbr_mesh_gx2.gsh de 36 PS_GPR)
// Fbm3D de 2 octavas, sin pow constante y con inversesqrt para 60 FPS en AMD Latte R700.
// ============================================================================

// Sin layout(location=...) para coincidir con pbr_mesh_gx2.vert (#version 330 core)
in vec3 vWorldPos;
in vec3 vNormal;
in vec2 vTexCoord;
in vec4 vColor;
in vec4 vLightSpacePos;
in vec3 vLocalPos;

// Empaquetado contiguo de 11 vec4 (44 floats):
// [0]  = (camPos.xyz, shadingMode=8)
// [1]  = (dirLightDir.xyz, elapsedTime)
// [2]  = (dirLightColor.rgb, dirIntensityOrShininess)
// [3]  = (albedoTint.rgb, subMode: 0=Terrain, 1=Marble, 2=Wood)
// [4]  = (toonOn, silhouetteOn, bumpScale, ambient)
// [5]  = (roughness, metallic, activePointLights, spotIntensity)
// [6]  = pointLightPosRadius[0] (hoguera)
// [10] = pointLightColInt[0]    (hoguera)
uniform vec4 uPsData[11];

out vec4 fragColor;

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

float Fbm3DFast(vec3 p) {
    // [OPT-468] 1 octava Noise3D (8 hashes) + modulacion trigonometrica analitica (0 hashes extra, -50% ALU)
    float n0 = Noise3D(p);
    float n1 = 0.5 + 0.5 * (sin(p.x * 3.1 + p.z * 2.7) * cos(p.y * 2.9 - p.x * 1.7));
    return 0.72 * n0 + 0.28 * n1;
}

void main() {
    vec3 toCam = uPsData[0].xyz - vWorldPos;
    vec3 V8 = toCam * inversesqrt(max(dot(toCam, toCam), 1e-6));
    vec3 N8 = normalize(vNormal);
    vec3 L8 = normalize(uPsData[1].xyz);
    float subMode = uPsData[3].w;

    vec3 albedo8;
    if (subMode < 0.5) {
        // [OPT-468] Splatting de terreno con variacion trigonometrica analitica (0 hashes 3D por pixel)
        float n0 = 0.5 + 0.5 * sin(vWorldPos.x * 0.85 + sin(vWorldPos.z * 0.75));
        float h = clamp(vColor.r + (n0 - 0.5) * 0.08, 0.0, 1.0);
        float slope = clamp(vColor.g, 0.0, 1.0);
        float detail = 0.88 + 0.24 * n0;

        vec3 sandCol  = vec3(0.56, 0.46, 0.32) * detail;
        vec3 grassCol = vec3(0.20, 0.48, 0.19) * detail;
        vec3 rockCol  = vec3(0.42, 0.40, 0.39) * detail;
        vec3 snowCol  = vec3(0.92, 0.95, 0.99);

        vec3 ground = mix(sandCol, grassCol, smoothstep(0.08, 0.24, h));
        ground = mix(ground, rockCol, smoothstep(0.18, 0.42, slope));
        ground = mix(ground, snowCol, smoothstep(0.62, 0.80, h) * (1.0 - smoothstep(0.32, 0.58, slope)));
        albedo8 = ground;
    } else if (subMode < 1.5) {
        // Marmol 3D Procedural (2 octavas + sqrt en lugar de pow(0.55))
        vec3 p3 = vLocalPos * 2.4;
        float turb = Fbm3DFast(p3);
        float vein = sqrt(clamp(0.5 + 0.5 * sin(p3.x * 3.8 + p3.y * 1.4 + turb * 6.2), 0.0, 1.0));
        albedo8 = mix(vec3(0.16, 0.20, 0.26), vec3(0.95, 0.94, 0.90), vein);
    } else {
        // Madera 3D Procedural (anillos concentricos + turbulencia 2 octavas)
        vec3 p3 = vLocalPos * 2.2;
        float turb = Fbm3DFast(p3 * 1.35);
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
    float diff8 = toonOn ? ((floor(NdotL8 * 4.0) + 0.35) * 0.25) : NdotL8;
    vec3 hv8 = V8 + L8;
    vec3 H8 = hv8 * inversesqrt(max(dot(hv8, hv8), 1e-6));
    float NdotH8 = max(dot(N8, H8), 0.0);
    float specBase = NdotH8 * NdotH8;
    specBase = specBase * specBase;
    specBase = specBase * specBase;
    specBase = specBase * specBase; // NdotH^16 sin pow()
    float spec8 = toonOn ? (step(0.92, NdotH8) * 0.45) : (specBase * specBase * 0.28);

    vec3 hemi8 = mix(vec3(0.12, 0.14, 0.18), vec3(0.30, 0.38, 0.50), N8.y * 0.5 + 0.5);
    vec3 col8 = albedo8 * (hemi8 + uPsData[2].rgb * (diff8 * 1.10)) + uPsData[2].rgb * spec8;

    // Luz puntual calida de la hoguera (slot 0) sin length() ni divisiones lentas
    if (uPsData[5].z > 0.5) {
        vec4 pPR = uPsData[6];
        vec4 pCI = uPsData[10];
        vec3 toL = pPR.xyz - vWorldPos;
        float distSq = dot(toL, toL);
        float rad = max(pPR.w, 0.1);
        if (distSq < rad * rad) {
            float invD = inversesqrt(max(distSq, 1e-6));
            float d = distSq * invD;
            vec3 Lp = toL * invD;
            float ff = clamp(1.0 - d / rad, 0.0, 1.0);
            float att = (ff * ff) / (1.0 + 0.20 * distSq);
            float ndl = max(dot(N8, Lp), 0.0);
            col8 += albedo8 * pCI.rgb * (pCI.w * att * (toonOn ? (floor(ndl * 3.0 + 0.5) * (1.0 / 3.0)) : ndl));
        }
    }

    fragColor = vec4(clamp(col8, 0.0, 1.0), 1.0);
}

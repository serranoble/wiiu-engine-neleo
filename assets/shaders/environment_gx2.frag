// Fragment stage para Demo 10 (SceneEnvironmentGX2).
// Soporta texturizado GX2 en silicio, sombreado Phong con sombras de nubes,
// agua transparente con oleaje suave, y cielo con nubes procedurales y glow solar.
#version 330 core

in vec3 vWorldPos;
in vec3 vNormal;
in vec2 vTexCoord;
in vec4 vColor;

uniform sampler2D uTexture;

uniform vec4 uCamPosTime;      // KC2[8]: xyz = uCamPos, w = uTime
uniform vec4 uLightDirSun;     // KC2[9]: xyz = uLightDir (hacia el sol), w = uSunVisibility
uniform vec4 uLightColorGlow;  // KC2[10]: rgb = uLightColor, w = uSunGlowFactor
uniform vec4 uWindParams;      // KC2[11]: xy = uWindDir, z = uCloudDensity, w = uCloudCoverage
uniform vec4 uMaterialMode;    // KC2[12]: x = matType (0: Sky, 1: Textured, 2: Water, 3: SunOrb), y = uvScale, z = waterLevel, w = roughness

out vec4 fragColor;

// Generador de ruido hash 2D branchless para ALUs VLIW5 de AMD Latte
float hash21(vec2 p) {
    p = fract(p * vec2(123.34, 456.21));
    p += dot(p, p + 45.32);
    return fract(p.x * p.y);
}

// Ruido de valor 2D bilineal con interpolación Hermite
float valueNoise(vec2 p) {
    vec2 i = floor(p);
    vec2 f = fract(p);
    vec2 u = f * f * (3.0 - 2.0 * f);
    float a = hash21(i);
    float b = hash21(i + vec2(1.0, 0.0));
    float c = hash21(i + vec2(0.0, 1.0));
    float d = hash21(i + vec2(1.0, 1.0));
    return mix(mix(a, b, u.x), mix(c, d, u.x), u.y);
}

// 3 Octavas de ruido fBm desplegadas estáticamente (stack_size = 0)
float fbmClouds(vec2 uv) {
    float val = 0.5000 * valueNoise(uv);
    val += 0.2500 * valueNoise(uv * 2.03 + vec2(1.7, 9.2));
    val += 0.1250 * valueNoise(uv * 4.01 + vec2(8.3, 2.8));
    return val;
}

void main() {
    float matType = uMaterialMode.x;
    float time = uCamPosTime.w;
    vec3 camPos = uCamPosTime.xyz;
    vec3 lightDir = normalize(uLightDirSun.xyz); // Vector unitario hacia el sol
    vec2 windOffset = uWindParams.xy * (time * 0.035);

    // =========================================================================
    // CASO 0: BÓVEDA CELESTE (SKY DOME) CON SOL, CORONA Y NUBES PROCEDURALES
    // =========================================================================
    if (matType < 0.5) {
        vec3 rayDir = normalize(vWorldPos - camPos);
        float rayY = clamp(rayDir.y, 0.0, 1.0);

        // Gradiente atmosférico cenit (azul cielo profundo) a horizonte (celeste luminoso / cálido)
        vec3 zenithColor = vec3(0.12, 0.38, 0.82);
        vec3 horizonColor = vec3(0.72, 0.85, 0.98);
        vec3 skyGrad = mix(horizonColor, zenithColor, pow(rayY, 0.55));

        // Proyección planar de nubes procedurales
        float cloudY = max(rayDir.y, 0.08);
        vec2 cloudUV = (rayDir.xz / cloudY) * 0.28 + windOffset;
        float cloudSample = fbmClouds(cloudUV);
        float cloudCoverage = clamp(uWindParams.w, 0.15, 0.88);
        float cloudDensity = smoothstep(1.0 - cloudCoverage, 1.02 - cloudCoverage, cloudSample * 1.15);

        // Sombras internas e iluminación de bordes en las nubes
        vec3 cloudBase = vec3(0.88, 0.91, 0.96);
        vec3 cloudHighlight = vec3(1.0, 0.98, 0.94) * (1.0 + 0.35 * max(dot(rayDir, lightDir), 0.0));
        vec3 cloudColor = mix(cloudBase * 0.80, cloudHighlight, cloudSample);

        // Sol analítico y resplandor (Glow)
        float cosTheta = dot(rayDir, lightDir);
        float sunDisc = smoothstep(0.9970, 0.9992, cosTheta);
        float sunGlow = pow(max(cosTheta, 0.0), 20.0) * 0.80 + pow(max(cosTheta, 0.0), 3.0) * 0.25;

        // Oclusión del sol por las nubes en la línea de visión del astro
        vec2 sunCloudUV = (lightDir.xz / max(lightDir.y, 0.08)) * 0.28 + windOffset;
        float sunCloudSample = fbmClouds(sunCloudUV);
        float sunOcclusion = smoothstep(1.0 - cloudCoverage, 1.02 - cloudCoverage, sunCloudSample * 1.15);
        float sunVis = clamp(1.0 - sunOcclusion * 1.35, 0.12, 1.0);

        vec3 sunColor = uLightColorGlow.rgb * 2.5;
        vec3 glowColor = uLightColorGlow.rgb * (sunGlow * uLightColorGlow.w * sunVis);

        // Composición final del cielo: cielo base + sol + resplandor, atenuados bajo nubes
        vec3 litSky = skyGrad + sunDisc * sunColor * sunVis + glowColor;
        vec3 finalSky = mix(litSky, cloudColor, cloudDensity * 0.94);

        fragColor = vec4(finalSky, 1.0);
        return;
    }

    // =========================================================================
    // CASO 3: ORBE SOLAR DIRECTO / GLOW DISK
    // =========================================================================
    if (matType > 2.5) {
        vec3 emissiveSun = uLightColorGlow.rgb * (1.8 + 0.4 * uLightColorGlow.w);
        fragColor = vec4(emissiveSun, 1.0);
        return;
    }

    // =========================================================================
    // FACTOR DE SOMBRA DE NUBES SOBRE LA SUPERFICIE (CLOUD SHADOWS)
    // =========================================================================
    // Proyecta la posición del mundo hacia el plano de nubes siguiendo el vector solar
    float cloudHeight = 18.0;
    vec2 shadowUV = (vWorldPos.xz + lightDir.xz * (cloudHeight - vWorldPos.y) / max(lightDir.y, 0.15)) * 0.045 + windOffset;
    float shadowNoise = fbmClouds(shadowUV);
    float cloudCoverage = clamp(uWindParams.w, 0.1, 0.9);
    float cloudShadowFactor = smoothstep(1.0 - cloudCoverage, 1.05 - cloudCoverage, shadowNoise * uWindParams.z);
    float directSunShadow = clamp(1.0 - cloudShadowFactor * 0.75, 0.25, 1.0);

    // =========================================================================
    // CASO 2: LAGUNA DE AGUA TRANSPARENTE CON OLEAJE SUAVE Y ESPUMA DE ORILLA [OPT-195]
    // =========================================================================
    if (matType > 1.5 && matType < 2.5) {
        // Normal perturbada con oleaje de río tranquilo (2 frecuencias lentas)
        vec2 rippleUV1 = vWorldPos.xz * 1.4 + vec2(time * 0.22, time * 0.14);
        vec2 rippleUV2 = vWorldPos.xz * 2.8 - vec2(time * 0.18, time * 0.25);
        vec2 wave1 = vec2(sin(rippleUV1.x * 3.0 + rippleUV1.y * 2.0), cos(rippleUV1.x * 2.0 - rippleUV1.y * 3.0)) * 0.045;
        vec2 wave2 = vec2(cos(rippleUV2.x * 4.0 - rippleUV2.y * 3.0), sin(rippleUV2.x * 3.0 + rippleUV2.y * 4.0)) * 0.025;
        vec3 waterNormal = normalize(vec3(wave1.x + wave2.x, 1.0, wave1.y + wave2.y));

        // Reflejo Fresnel
        vec3 viewDir = normalize(camPos - vWorldPos);
        float NdotV = max(dot(viewDir, waterNormal), 0.0);
        float fresnel = 0.15 + 0.85 * pow(1.0 - NdotV, 4.0);

        // Iluminación solar sobre el agua
        float NdotL = max(dot(waterNormal, lightDir), 0.0);
        vec3 halfVec = normalize(lightDir + viewDir);
        float spec = pow(max(dot(waterNormal, halfVec), 0.0), 48.0) * directSunShadow;

        vec3 deepColor = vec3(0.04, 0.22, 0.38);
        vec3 shallowColor = vec3(0.12, 0.48, 0.60);
        vec3 baseWater = mix(deepColor, shallowColor, 0.35);

        vec3 sunReflection = uLightColorGlow.rgb * spec * 2.2;
        vec3 finalWater = baseWater * 0.38 + sunReflection + (shallowColor * fresnel * 0.45);

        // [OPT-195] Suavizado y Espuma Animada en la Orilla de la Laguna (Shoreline Foam)
        // La cuenca de la laguna está centrada en (7.0, 7.0) con radio ~3.8m.
        float distLagoonCenter = length(vWorldPos.xz - vec2(7.0, 7.0));
        float shoreFactor = smoothstep(2.3, 4.15, distLagoonCenter);
        float shoreWave = sin(distLagoonCenter * 14.0 - time * 2.6) * 0.5 + 0.5;
        float shoreFoam = smoothstep(0.65, 0.94, shoreWave * shoreFactor);

        // Espuma de contacto con el peñasco fluvial sumergido en (6.2, 6.4)
        float distRock2 = length(vWorldPos.xz - vec2(6.2, 6.4));
        float rockContactFoam = smoothstep(1.10, 0.55, distRock2) * (sin(distRock2 * 18.0 - time * 3.0) * 0.5 + 0.5);
        float totalFoam = clamp(shoreFoam + rockContactFoam * 0.80, 0.0, 1.0);

        vec3 foamColor = vec3(0.92, 0.98, 1.0);
        finalWater = mix(finalWater, foamColor, totalFoam * 0.70);
        finalWater = mix(finalWater, vec3(0.18, 0.58, 0.64), shoreFactor * 0.42);

        // Alfa translúcido calibrado: ~0.35 mirando vertical para máxima transparencia de lecho y rocas sumergidas,
        // aumentando con Fresnel a ~0.72 en ángulos rasantes y hasta 0.88 en las crestas de espuma.
        float waterAlpha = clamp(0.32 + fresnel * 0.38 + totalFoam * 0.28, 0.32, 0.88);
        fragColor = vec4(finalWater, waterAlpha);
        return;
    }

    // =========================================================================
    // CASO 1: GEOMETRÍA TEXTURIZADA CON MICRO-RELIEVE ANALÍTICO [OPT-197]
    // =========================================================================
    vec2 uv = vTexCoord * uMaterialMode.y;
    vec4 texColor = texture(uTexture, uv);
    vec3 albedo = texColor.rgb * vColor.rgb;

    // Distinción entre Visor de Estudio (uWindParams.z == 0.0) y Paisaje Exterior (uWindParams.z > 0.0)
    bool isStudio = (uWindParams.z < 0.05);

    // [OPT-197] Micro-Relieve / Bump Mapping a partir de la textura en VRAM
    // Para modelos de estudio usa paso de alta definición 2048x2048 (0.00048828125)
    // con proyección ortogonal sobre el plano tangente para blindar la normal contra inversiones.
    float stepSize = (uMaterialMode.z < 0.0) ? 0.00048828125 : 0.0078125;
    float bumpScale = (uMaterialMode.z < 0.0) ? abs(uMaterialMode.z) : 1.8;
    vec2 dUV = vec2(stepSize, stepSize);
    float hC = dot(texColor.rgb, vec3(0.299, 0.587, 0.114));
    float hR = dot(texture(uTexture, uv + vec2(dUV.x, 0.0)).rgb, vec3(0.299, 0.587, 0.114));
    float hU = dot(texture(uTexture, uv + vec2(0.0, dUV.y)).rgb, vec3(0.299, 0.587, 0.114));
    vec3 bumpGrad = vec3((hC - hR) * bumpScale, 1.0, (hC - hU) * bumpScale);
    vec3 baseN = normalize(vNormal);

    // Proyección ortogonal al plano tangente: g_perp = g - baseN * dot(g, baseN)
    // Garantiza que la perturbación nunca perfore la geometría ni cree normales degeneradas/inversas
    vec3 rawGrad = vec3(bumpGrad.x, 0.0, bumpGrad.z);
    vec3 tanGrad = rawGrad - baseN * dot(rawGrad, baseN);
    vec3 N = (bumpScale > 0.001)
        ? normalize(baseN + tanGrad * (0.35 * (1.0 - uMaterialMode.w * 0.5)))
        : baseN;

    vec3 viewDir = normalize(camPos - vWorldPos);

    vec3 finalLit;
    if (isStudio) {
        // [OPT-322] Iluminación de Estudio Profesional de 3 Puntos (Key, Fill, Rim) + Half-Lambert Wrap:
        // Evita que las caras opuestas a la luz principal queden sepultadas en negro absoluto,
        // preservando la riqueza cromática y detalle de las texturas pintadas a mano en cualquier ángulo.

        // 1. Luz Principal (Key Light): Half-Lambert / Disney diffuse wrap para transición suave
        float NdotL = dot(N, lightDir);
        float wrapKey = clamp((NdotL + 0.35) / 1.35, 0.0, 1.0);
        vec3 keyDiffuse = uLightColorGlow.rgb * (wrapKey * wrapKey);

        // 2. Luz de Relleno (Fill Light): Opuesta en XZ con elevación moderada para bañar sombras
        vec3 fillDir = normalize(vec3(-lightDir.x, 0.45, -lightDir.z));
        float NdotFill = dot(N, fillDir);
        float wrapFill = clamp((NdotFill + 0.30) / 1.30, 0.0, 1.0);
        vec3 fillDiffuse = (uLightColorGlow.rgb * 0.45 + vec3(0.12, 0.14, 0.18)) * (wrapFill * wrapFill * 0.45);

        // 3. Luz de Silueta / Contorno (Rim / Back Light): Estrictamente acotada a la arista de silueta
        // Exponente 5.0 y ganancia acotada para independizar por completo la luminosidad de las caras del zoom
        float NdotV = clamp(dot(N, viewDir), 0.0, 1.0);
        float rimFactor = pow(1.0 - NdotV, 5.0);
        float rimMask = clamp(-NdotL * 0.5 + 0.5, 0.20, 1.0);
        vec3 rimLight = (uLightColorGlow.rgb * 0.40 + vec3(0.12, 0.14, 0.18)) * (rimFactor * rimMask * 0.25);

        // 4. Luz Ambiental de Estudio Dinámica: Modulada coherentemente con la temperatura de la etapa día/noche
        vec3 baseAmb = uLightColorGlow.rgb * 0.28 + vec3(0.10, 0.11, 0.14);
        vec3 ambient = baseAmb * (N.y * 0.25 + 0.75);

        // 5. Especular Blinn-Phong de Estudio
        vec3 halfVec = normalize(lightDir + viewDir);
        float specFactor = (1.0 - uMaterialMode.w) * 0.30;
        float spec = pow(max(dot(N, halfVec), 0.0), 24.0) * specFactor;

        finalLit = albedo * (ambient + keyDiffuse + fillDiffuse) + uLightColorGlow.rgb * spec + rimLight;
    } else {
        // Modo Paisaje Exterior (Demo 10): Luz solar directa + cielo azul / rebote terrestre
        float NdotL = max(dot(N, lightDir), 0.0);
        vec3 diffuse = uLightColorGlow.rgb * (NdotL * directSunShadow);

        vec3 skyAmb = vec3(0.35, 0.42, 0.52);
        vec3 groundAmb = vec3(0.18, 0.15, 0.12);
        vec3 ambient = mix(groundAmb, skyAmb, N.y * 0.5 + 0.5);

        vec3 halfVec = normalize(lightDir + viewDir);
        float specFactor = (1.0 - uMaterialMode.w) * 0.30;
        float spec = pow(max(dot(N, halfVec), 0.0), 20.0) * specFactor * directSunShadow;

        finalLit = albedo * (ambient + diffuse) + uLightColorGlow.rgb * spec;
    }

    fragColor = vec4(finalLit, texColor.a * vColor.a);
}

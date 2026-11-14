// Pixel stage de Demo 9: Luz y Sombras 3D GX2 con Ray Tracing por Hardware (100% branchless, stack_size = 0).
// Incluye Ray Tracing en tiempo real con reflejos analíticos de segundo orden (esfera, cubo, pirámide, orbe y suelo),
// escalado de resolución 648p -> 720p, OPT-155 (Grid AA), OPT-125/127/135 (Sombras Exactas PCSS),
// OPT-168 (Contact AO), OPT-169 (GI Color Bleeding) y OPT-171 (Volumetric Light Haze).
#version 330 core
in vec3 vWorldPos;
in vec3 vNormal;
in vec4 vColor;

out vec4 FragColor;

uniform vec4 uLightPos;       // KC2[0]: xyz = light pos (0.0, height, 0.0), w = unused
uniform vec4 uLightColor;     // KC2[1]: rgb = light color, w = brightness / intensity
uniform vec4 uCamPos;         // KC2[2]: xyz = cam pos, w = unused
uniform vec4 uAmbient;        // KC2[3]: rgb = ambient color, w = shininess
uniform vec4 uMaterialParams; // KC2[4]: x = isFloor, y = isLightOrb, z = isRaytracing, w = is648p

void main() {
    // 0. Parámetros de configuración en tiempo real
    float isFloor = clamp(uMaterialParams.x, 0.0, 1.0);
    float isOrb = clamp(uMaterialParams.y, 0.0, 1.0);
    float isRT = clamp(uMaterialParams.z, 0.0, 1.0);
    float is648p = clamp(uMaterialParams.w, 0.0, 1.0);

    // [648p vs 720p] Cuantización espacial de pantalla para simulación de buffer 1152x648 escalado a 1280x720
    vec2 screenGrid = floor(gl_FragCoord.xy * 0.90) * (1.0 / 0.90);
    vec2 qDelta = (screenGrid - gl_FragCoord.xy) * (is648p * 0.0018);

    // 1. Vectores de iluminación puntual y cámara con protección numérica branchless
    vec3 L_vec = uLightPos.xyz - vWorldPos;
    float distSq = dot(L_vec, L_vec) + 0.0001;
    float invDist = inversesqrt(distSq);
    float dist = distSq * invDist;
    vec3 L = L_vec * invDist;

    vec3 V_vec = uCamPos.xyz - vWorldPos + vec3(qDelta.x, 0.0001, qDelta.y);
    float camDistSq = dot(V_vec, V_vec) + 0.0001;
    float invCamDist = inversesqrt(camDistSq);
    float camDist = camDistSq * invCamDist;
    vec3 V = V_vec * invCamDist;

    vec3 N = normalize(vNormal + vec3(0.0, 0.0001, 0.0));
    vec3 H = normalize(L + V + vec3(0.0, 0.0001, 0.0));

    // 2. Atenuación física radial desde el centro (0, height, 0)
    float brightness = max(uLightColor.w, 0.0);
    float atten = brightness / (1.0 + 0.075 * dist + 0.020 * distSq);

    // 3. [OPT-155] Cuadrícula del terreno con Anti-Aliasing analítico adaptativo a la distancia
    vec2 gridMod = fract(abs(vWorldPos.xz + qDelta * 0.5));
    vec2 gridEdge = min(gridMod, 1.0 - gridMod);
    float minGrid = min(gridEdge.x, gridEdge.y);
    float aaWidth = clamp(0.024 + camDist * 0.0038, 0.024, 0.11);
    float distFade = clamp(1.30 - camDist * 0.052, 0.22, 1.0);
    float lineMask = (1.0 - smoothstep(0.014, aaWidth, minGrid)) * distFade;
    float rawGrid = 1.0 - 0.32 * lineMask;
    float gridLine = mix(1.0, rawGrid, isFloor);

    // 4. [OPT-125 / OPT-127 / OPT-135] Sombras proyectadas con silueta exacta (Cubo, Pirámide, Esfera)
    // y Penumbra con Endurecimiento por Contacto (Contact-Hardening Soft Shadows)
    // Objeto 1: Cubo Coral en (-2.6, 0.75, -1.5), semilado 0.75 (Métrica SDF Box 3D)
    vec3 v1 = vec3(-2.6, 0.75, -1.5) - vWorldPos;
    float t1 = clamp(dot(v1, L), 0.0, dist);
    vec3 r1 = vWorldPos + L * t1;
    vec3 b1 = max(abs(r1 - vec3(-2.6, 0.75, -1.5)) - vec3(0.65, 0.69, 0.65), 0.0);
    float pen1 = 0.032 + 0.28 * (t1 / max(dist - t1, 0.45));
    float sh1 = smoothstep(0.004, pen1, dot(b1, b1));

    // Objeto 2: Pirámide Esmeralda en (2.6, 0.0, -1.5), base 0.85, cúspide y = 1.75 (Sección ahusada)
    vec3 v2 = vec3(2.6, 0.65, -1.5) - vWorldPos;
    float t2 = clamp(dot(v2, L), 0.0, dist);
    vec3 r2 = vWorldPos + L * t2;
    float hNorm2 = clamp(r2.y * 0.5714, 0.0, 1.0);
    float pyrHalfW = 0.80 * (1.0 - hNorm2);
    vec2 xzDiff2 = max(abs(r2.xz - vec2(2.6, -1.5)) - vec2(pyrHalfW), 0.0);
    float yDiff2 = max(abs(r2.y - 0.875) - 0.875, 0.0);
    float pen2 = 0.028 + 0.26 * (t2 / max(dist - t2, 0.45));
    float sh2 = smoothstep(0.003, pen2, dot(xzDiff2, xzDiff2) + yDiff2 * yDiff2);

    // Objeto 3: Esfera Zafiro en (0.0, 0.85, 2.8), radio 0.85 (Sección esférica + PCSS)
    vec3 v3 = vec3(0.0, 0.85, 2.8) - vWorldPos;
    float t3 = clamp(dot(v3, L), 0.0, dist);
    vec3 p3 = v3 - L * t3;
    float pen3 = 0.14 + 0.34 * (t3 / max(dist - t3, 0.45));
    float sh3 = smoothstep(0.46, 0.46 + pen3, dot(p3, p3));

    float rawShadow = min(sh1, min(sh2, sh3));
    float shadowFactor = mix(1.0, rawShadow, isFloor);

    // 5. [OPT-168 / OPT-169] Oclusión Ambiental de Contacto (Contact AO) y Rebote Indirecto (Color Bleeding GI)
    vec2 dC1 = vWorldPos.xz - vec2(-2.6, -1.5);
    vec2 dC2 = vWorldPos.xz - vec2( 2.6, -1.5);
    vec2 dC3 = vWorldPos.xz - vec2( 0.0,  2.8);
    float distC1Sq = dot(dC1, dC1);
    float distC2Sq = dot(dC2, dC2);
    float distC3Sq = dot(dC3, dC3);

    float contactAOFloor = smoothstep(0.35, 1.15, min(distC1Sq, min(distC2Sq, distC3Sq)));
    float contactAOObj = clamp(0.74 + 0.26 * vWorldPos.y, 0.74, 1.0) * (0.84 + 0.16 * N.y);
    float aoFactor = mix(contactAOObj, 0.72 + 0.28 * contactAOFloor, isFloor);

    float giScale = 0.15 * clamp(brightness, 0.0, 1.6) * isFloor;
    vec3 giBounce = (vec3(0.92, 0.37, 0.29) / (1.0 + distC1Sq * 0.80) +
                     vec3(0.27, 0.86, 0.57) / (1.0 + distC2Sq * 0.80) +
                     vec3(0.37, 0.61, 0.98) / (1.0 + distC3Sq * 0.80)) * giScale;

    // 6. Iluminación Lambertiana + Especular Blinn-Phong + [OPT-171] Halo Volumétrico
    vec3 surfBase = vColor.rgb * gridLine;
    float NdotL = max(dot(N, L), 0.0);
    float lightActive = clamp(brightness * 2.0, 0.0, 1.0);
    float ambShadow = mix(1.0, mix(0.38, 1.0, shadowFactor), lightActive);

    vec3 ambient = uAmbient.rgb * surfBase * ambShadow * aoFactor + giBounce * surfBase;
    vec3 diffuse = NdotL * uLightColor.rgb * surfBase * atten * 1.38 * shadowFactor;

    float NdotH = max(dot(N, H), 0.0);
    float s2 = NdotH * NdotH;
    float s4 = s2 * s2;
    float s8 = s4 * s4;
    float s16 = s8 * s8;
    float s32 = s16 * s16;
    vec3 specular = s32 * uLightColor.rgb * atten * 0.38 * shadowFactor;

    // [OPT-171] Dispersión atmosférica radial cerca del orbe de luz central
    vec3 volHaze = uLightColor.rgb * (0.075 * clamp(brightness, 0.0, 2.0) / (1.0 + 0.32 * distSq));

    vec3 litColor = clamp(ambient + diffuse + specular + volHaze, 0.0, 1.0);

    // 7. Ray Tracing GX: Trazado Analítico de Rayos de Reflexión (100% Branchless)
    vec3 rayOrig = vWorldPos + N * 0.025;
    vec3 rayDir = reflect(-V, N);
    vec3 invRayDir = 1.0 / (rayDir + (step(0.0, rayDir) * 2.0 - 1.0) * 0.0001);

    // Objeto A: Orbe de Luz Central Emisivo (Esfera r = 0.32 en y = uLightPos.y)
    vec3 ocOrb = rayOrig - vec3(0.0, uLightPos.y, 0.0);
    float bOrb = dot(ocOrb, rayDir);
    float cOrb = dot(ocOrb, ocOrb) - 0.1024;
    float discOrb = bOrb * bOrb - cOrb;
    float tOrb = -bOrb - sqrt(max(discOrb, 0.0));
    float hitOrb = step(0.0, discOrb) * step(0.05, tOrb);
    float distOrb = mix(1000.0, tOrb, hitOrb);
    vec3 colOrb = uLightColor.rgb * (1.8 + brightness * 0.5);

    // Objeto B: Cubo Coral en (-2.6, 0.75, -1.5), r = 0.75
    vec3 tMinBox = (vec3(-3.35, 0.0, -2.25) - rayOrig) * invRayDir;
    vec3 tMaxBox = (vec3(-1.85, 1.5, -0.75) - rayOrig) * invRayDir;
    vec3 tb0 = min(tMinBox, tMaxBox);
    vec3 tb1 = max(tMinBox, tMaxBox);
    float tNearBox = max(max(tb0.x, tb0.y), tb0.z);
    float tFarBox  = min(min(tb1.x, tb1.y), tb1.z);
    float hitBox = step(tNearBox, tFarBox) * step(0.05, tNearBox);
    float distBox = mix(1000.0, tNearBox, hitBox);
    vec3 colBox = vec3(0.92, 0.37, 0.29) * (0.35 + 0.65 * atten);

    // Objeto C: Pirámide Esmeralda en (2.6, 0.0, -1.5)
    vec3 tMinPyr = (vec3(1.75, 0.0, -2.35) - rayOrig) * invRayDir;
    vec3 tMaxPyr = (vec3(3.45, 1.75, -0.65) - rayOrig) * invRayDir;
    vec3 tp0 = min(tMinPyr, tMaxPyr);
    vec3 tp1 = max(tMinPyr, tMaxPyr);
    float tNearPyr = max(max(tp0.x, tp0.y), tp0.z);
    float tFarPyr  = min(min(tp1.x, tp1.y), tp1.z);
    vec3 pPyr = rayOrig + rayDir * tNearPyr;
    float pyrH = clamp(pPyr.y / 1.75, 0.0, 1.0);
    float pyrW = 0.85 * (1.0 - pyrH);
    float pyrD = max(abs(pPyr.x - 2.6), abs(pPyr.z - (-1.5))) - pyrW;
    float hitPyr = step(tNearPyr, tFarPyr) * step(0.05, tNearPyr) * step(pyrD, 0.06);
    float distPyr = mix(1000.0, tNearPyr, hitPyr);
    vec3 colPyr = vec3(0.27, 0.86, 0.57) * (0.35 + 0.65 * atten);

    // Objeto D: Esfera Zafiro en (0.0, 0.85, 2.8), r = 0.85
    vec3 ocSph = rayOrig - vec3(0.0, 0.85, 2.8);
    float bSph = dot(ocSph, rayDir);
    float cSph = dot(ocSph, ocSph) - 0.7225;
    float discSph = bSph * bSph - cSph;
    float tSph = -bSph - sqrt(max(discSph, 0.0));
    float hitSph = step(0.0, discSph) * step(0.05, tSph);
    float distSph = mix(1000.0, tSph, hitSph);
    vec3 colSph = vec3(0.37, 0.61, 0.98) * (0.35 + 0.65 * atten);

    // Objeto E: Suelo reflectivo (cuando el rayo apunta hacia abajo desde un objeto 3D)
    float isDownRay = step(0.005, -rayDir.y);
    float tFlr = -rayOrig.y / min(rayDir.y, -0.005);
    vec3 pFlr = rayOrig + rayDir * tFlr;
    float hitFlr = isDownRay * step(0.05, tFlr) * step(abs(pFlr.x), 12.0) * step(abs(pFlr.z), 12.0) * (1.0 - isFloor);
    float distFlr = mix(1000.0, tFlr, hitFlr);
    vec2 flrMod = fract(abs(pFlr.xz));
    float flrLine = 1.0 - 0.3 * step(min(flrMod.x, min(1.0 - flrMod.x, min(flrMod.y, 1.0 - flrMod.y))), 0.04);
    vec3 colFlr = vec3(0.18, 0.20, 0.25) * flrLine + uAmbient.rgb * 0.5;

    // Prueba de oclusión / profundidad mínima branchless
    float minT = min(distOrb, min(distBox, min(distPyr, min(distSph, distFlr))));
    float hasHit = step(minT, 990.0);

    float wOrb = step(abs(minT - distOrb), 0.02);
    float wBox = step(abs(minT - distBox), 0.02) * (1.0 - wOrb);
    float wPyr = step(abs(minT - distPyr), 0.02) * (1.0 - wOrb - wBox);
    float wSph = step(abs(minT - distSph), 0.02) * (1.0 - wOrb - wBox - wPyr);
    float wFlr = step(abs(minT - distFlr), 0.02) * (1.0 - wOrb - wBox - wPyr - wSph);

    vec3 reflColor = wOrb * colOrb + wBox * colBox + wPyr * colPyr + wSph * colSph + wFlr * colFlr;

    // Modulación de reflectividad según superficie (esfera = metálica 0.65, suelo = mármol 0.30, otros = 0.20)
    float isSphereObj = step(distC3Sq, 1.0) * (1.0 - isFloor);
    float reflFactor = mix(mix(0.20, 0.65, isSphereObj), 0.30, isFloor);

    vec3 finalLit = mix(litColor, litColor + reflColor * reflFactor * hasHit, isRT);

    // [OPT-177] Realce de Nitidez Adaptativo por Contraste (RCAS / FSR 1.0 style) en modo 648p
    vec3 sharpBoost = finalLit + (finalLit - diffuse * 0.75) * (is648p * 0.35);
    vec3 rcasLit = mix(finalLit, clamp(sharpBoost, 0.0, 1.0), is648p);

    // 8. Color del Orbe de Luz Central con núcleo incandescente Fresnel
    float intensity = clamp(brightness, 0.0, 2.0);
    float orbCore = max(dot(N, V), 0.0);
    vec3 bulbOff = vec3(0.08, 0.09, 0.12);
    vec3 bulbLit = clamp(uLightColor.rgb * (0.85 + 0.35 * intensity) + vec3(0.35 * orbCore * intensity), 0.0, 1.0);
    vec3 orbColor = mix(bulbOff, bulbLit, clamp(intensity * 4.0, 0.0, 1.0));

    // 9. Selección lineal 100% branchless (stack_size = 0 en R700)
    FragColor = vec4(mix(rcasLit, orbColor, isOrb), 1.0);
}

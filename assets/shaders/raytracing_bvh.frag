#version 330 core
in vec2 vUV;
out vec4 FragColor;

uniform vec4 uCamPos;           // xyz = pos, w = tanHalfFovY
uniform vec4 uCamForward;       // xyz = forward, w = aspect
uniform vec4 uCamRight;         // xyz = right, w = time
uniform vec4 uCamUp;            // xyz = up, w = 0
uniform vec4 uLightPos;         // xyz = lightPos, w = lightRadius (penumbra)
uniform vec4 uRenderFlags;      // x = bvhEnabled (0/1), y = shadowMode (0/1/2), z = heatmap (0/1), w = primCount
uniform vec4 uSunDirAndNodes;   // xyz = normalize(lightPos) precalculado CPU, w = float(bvhNodeCount)
uniform vec4 uScreenBounds;     // xy = ndcMin, zw = ndcMax (Screen-Space Early Reject)
uniform vec4 uShadowFloorBounds;// xy = minXZ, zw = maxXZ en plano y = -1.25
uniform vec4 uSpheres[12];      // xyz = center, w = radius
uniform vec4 uSphereColors[12]; // rgb = albedo, a = reflectivity
uniform vec4 uBvhNodes[48];     // 24 nodos Pre-Order Stackless: [2*i]=(min, payload/skip), [2*i+1]=(max, primCount)

float sphereIntersect(vec3 ro, vec3 rd, vec3 center, float radius) {
    vec3 oc = ro - center;
    float b = dot(oc, rd);
    float c = dot(oc, oc) - radius * radius;
    float disc = b * b - c;
    if (disc < 0.0) return -1.0;
    float t = -b - sqrt(disc);
    return (t > 0.001) ? t : -1.0;
}

bool aabbIntersect(vec3 ro, vec3 invDir, vec3 bMin, vec3 bMax, float maxT) {
    vec3 t0 = (bMin - ro) * invDir;
    vec3 t1 = (bMax - ro) * invDir;
    vec3 tMin = min(t0, t1);
    vec3 tMax = max(t0, t1);
    float tNear = max(max(tMin.x, tMin.y), tMin.z);
    float tFar  = min(min(tMax.x, tMax.y), tMax.z);
    return (tFar >= max(0.0, tNear)) && (tNear < maxT);
}

// Trazado primario o de reflejo usando BVH Stackless Pre-Order (0 arreglos en GPR) o fuerza bruta
void traceScene(vec3 ro, vec3 rd, int ignoreObj, float maxT,
                out float outT, out int outObj, out vec3 outNormal, out int outTests) {
    outT = maxT;
    outObj = -1;
    outNormal = vec3(0.0, 1.0, 0.0);
    outTests = 0;

    int primCountTotal = clamp(int(uRenderFlags.w + 0.5), 0, 12);

    if (uRenderFlags.x > 0.5) {
        vec3 rdSign = step(vec3(0.0), rd) * 2.0 - 1.0;
        vec3 invDir = 1.0 / (rdSign * max(abs(rd), vec3(1e-6)));
        int nodeCount = clamp(int(uSunDirAndNodes.w + 0.5), 0, 24);
        int nodeIdx = 0;

        for (int iter = 0; iter < nodeCount; ++iter) {
            if (nodeIdx >= nodeCount) break;
            vec4 nMin = uBvhNodes[nodeIdx * 2];
            vec4 nMax = uBvhNodes[nodeIdx * 2 + 1];
            int payload = int(nMin.w + 0.5);
            int pCount = int(nMax.w + 0.5);
            outTests++;

            if (!aabbIntersect(ro, invDir, nMin.xyz, nMax.xyz, outT)) {
                nodeIdx = (pCount > 0) ? (nodeIdx + 1) : payload;
                continue;
            }

            for (int i = 0; i < pCount; ++i) {
                int idx = payload + i;
                outTests++;
                if (idx == ignoreObj) continue;
                vec4 s = uSpheres[idx];
                float t = sphereIntersect(ro, rd, s.xyz, s.w);
                if (t > 0.001 && t < outT) {
                    outT = t;
                    outObj = idx;
                }
            }
            nodeIdx++;
        }
    } else {
        for (int i = 0; i < primCountTotal; ++i) {
            outTests++;
            if (i == ignoreObj) continue;
            vec4 s = uSpheres[i];
            float t = sphereIntersect(ro, rd, s.xyz, s.w);
            if (t > 0.001 && t < outT) {
                outT = t;
                outObj = i;
            }
        }
    }

    if (outObj >= 0) {
        outNormal = normalize((ro + rd * outT) - uSpheres[outObj].xyz);
    }
}

// Sombras Suaves Analíticas (Cap. 11.3: Penumbra por luz de área acelerada por BVH Stackless)
float traceShadow(vec3 ro, vec3 lightDir, float lightDist, int ignoreObj, int shadowMode, float lightRadius) {
    if (shadowMode == 0) return 1.0;

    float shadow = 1.0;
    int primCountTotal = clamp(int(uRenderFlags.w + 0.5), 0, 12);

    if (uRenderFlags.x > 0.5) {
        vec3 ldSign = step(vec3(0.0), lightDir) * 2.0 - 1.0;
        vec3 invDir = 1.0 / (ldSign * max(abs(lightDir), vec3(1e-6)));
        int nodeCount = clamp(int(uSunDirAndNodes.w + 0.5), 0, 24);
        int nodeIdx = 0;
        vec3 pad = vec3(shadowMode == 2 ? (1.25 * lightRadius + 0.25) : 0.01);

        for (int iter = 0; iter < nodeCount; ++iter) {
            if (nodeIdx >= nodeCount || shadow <= 0.001) break;
            vec4 nMin = uBvhNodes[nodeIdx * 2];
            vec4 nMax = uBvhNodes[nodeIdx * 2 + 1];
            int payload = int(nMin.w + 0.5);
            int pCount = int(nMax.w + 0.5);

            if (!aabbIntersect(ro, invDir, nMin.xyz - pad, nMax.xyz + pad, lightDist)) {
                nodeIdx = (pCount > 0) ? (nodeIdx + 1) : payload;
                continue;
            }

            for (int i = 0; i < pCount; ++i) {
                int idx = payload + i;
                if (idx == ignoreObj) continue;
                vec4 s = uSpheres[idx];
                vec3 oc = s.xyz - ro;
                float tProj = dot(oc, lightDir);
                if (tProj > 0.001 && tProj < lightDist) {
                    float distSq = max(0.0, dot(oc, oc) - tProj * tProj);
                    float distToSurf = sqrt(distSq) - s.w;
                    if (shadowMode == 1) {
                        if (distToSurf <= 0.0) {
                            shadow = 0.0;
                            break;
                        }
                    } else {
                        float penumbra = clamp(0.5 + 0.5 * distToSurf / (tProj * 0.12 * lightRadius + 0.01), 0.0, 1.0);
                        shadow = min(shadow, penumbra * penumbra * (3.0 - 2.0 * penumbra));
                    }
                }
            }
            nodeIdx++;
        }
    } else {
        for (int i = 0; i < primCountTotal; ++i) {
            if (shadow <= 0.001) break;
            if (i == ignoreObj) continue;
            vec4 s = uSpheres[i];
            vec3 oc = s.xyz - ro;
            float tProj = dot(oc, lightDir);
            if (tProj > 0.001 && tProj < lightDist) {
                float distSq = max(0.0, dot(oc, oc) - tProj * tProj);
                float distToSurf = sqrt(distSq) - s.w;
                if (shadowMode == 1) {
                    if (distToSurf <= 0.0) {
                        shadow = 0.0;
                        break;
                    }
                } else {
                    float penumbra = clamp(0.5 + 0.5 * distToSurf / (tProj * 0.12 * lightRadius + 0.01), 0.0, 1.0);
                    shadow = min(shadow, penumbra * penumbra * (3.0 - 2.0 * penumbra));
                }
            }
        }
    }
    return shadow;
}

vec3 evalSky(vec3 rd, vec3 lightDir) {
    float horizon = clamp(rd.y * 0.5 + 0.5, 0.0, 1.0);
    vec3 skyLow = vec3(0.06, 0.10, 0.22);
    vec3 skyHigh = vec3(0.02, 0.04, 0.12);
    vec3 sky = mix(skyLow, skyHigh, horizon * (0.35 + 0.65 * horizon));
    float s = max(0.0, dot(rd, lightDir));
    float s2 = s * s;
    float s4 = s2 * s2;
    float s16 = s4 * s4;
    float sun = s16 * s16 * s16;
    return sky + vec3(1.0, 0.90, 0.72) * sun * 0.85;
}

vec3 heatmapColor(int tests) {
    float t = clamp(float(tests) / 14.0, 0.0, 1.0);
    vec3 cold = vec3(0.05, 0.22, 0.68);
    vec3 mid  = vec3(0.10, 0.85, 0.45);
    vec3 hot  = vec3(1.00, 0.25, 0.08);
    if (t < 0.5) return mix(cold, mid, t * 2.0);
    return mix(mid, hot, (t - 0.5) * 2.0);
}

void main() {
    vec2 ndc = vUV * 2.0 - 1.0;
    float tanHalfFovY = uCamPos.w;
    float aspect = uCamForward.w;

    vec3 ro = uCamPos.xyz;
    vec3 rd = normalize(
        uCamForward.xyz +
        uCamRight.xyz * (ndc.x * aspect * tanHalfFovY) +
        uCamUp.xyz * (ndc.y * tanHalfFovY)
    );

    float nearestT = 1e9;
    int hitObj = -1;
    vec3 hitNormal = vec3(0.0, 1.0, 0.0);
    int primTests = 0;

    // Early Screen-Space Reject apoyado por CPU: descarta cielo/bordes en O(1)
    if (ndc.x >= uScreenBounds.x && ndc.x <= uScreenBounds.z &&
        ndc.y >= uScreenBounds.y && ndc.y <= uScreenBounds.w) {
        traceScene(ro, rd, -1, 1e9, nearestT, hitObj, hitNormal, primTests);
    }

    // Modo Heatmap para inspección visual del particionamiento espacial BVH + Screen Bounds
    if (uRenderFlags.z > 0.5) {
        vec3 heat = heatmapColor(primTests);
        if (hitObj >= 0) {
            heat = mix(heat, vec3(1.0), 0.18);
        }
        FragColor = vec4(heat, 1.0);
        return;
    }

    vec3 sunDir = uSunDirAndNodes.xyz;
    vec3 skyCol = evalSky(rd, sunDir);
    int shadowMode = int(uRenderFlags.y + 0.5);
    float lightRadius = max(0.05, uLightPos.w);

    // Plano de suelo infinito reflectante en y = -1.25
    float floorT = -1.0;
    if (rd.y < -0.005 && ro.y > -1.25) {
        floorT = (-1.25 - ro.y) / rd.y;
    }

    bool isSphereHit = (hitObj >= 0 && (floorT < 0.0 || nearestT < floorT));
    bool isFloorHit  = (!isSphereHit && floorT > 0.0 && floorT < 40.0);

    if (!isSphereHit && !isFloorHit) {
        FragColor = vec4(skyCol, 1.0);
        return;
    }

    vec3 surfPos    = isSphereHit ? (ro + rd * nearestT) : (ro + rd * floorT);
    vec3 surfNormal = isSphereHit ? hitNormal : vec3(0.0, 1.0, 0.0);
    int  ignoreId   = isSphereHit ? hitObj : -1;

    // 1. Sombra unificada (1 sola instancia de traceShadow en microcódigo R700)
    float shadow = 1.0;
    bool doShadow = (shadowMode > 0) && (isSphereHit ||
        (surfPos.x >= uShadowFloorBounds.x && surfPos.x <= uShadowFloorBounds.z &&
         surfPos.z >= uShadowFloorBounds.y && surfPos.z <= uShadowFloorBounds.w));
    vec3 toLight = uLightPos.xyz - surfPos;
    float lightDist = length(toLight);
    vec3 L = toLight / max(lightDist, 0.001);
    if (doShadow) {
        shadow = traceShadow(surfPos + surfNormal * 0.01, L, lightDist, ignoreId, shadowMode, lightRadius);
    }

    // 2. Reflexión unificada (1 sola instancia secundaria de traceScene en microcódigo R700)
    vec3 reflDir = reflect(rd, surfNormal);
    vec3 reflCol = evalSky(reflDir, sunDir);
    if (isSphereHit || floorT < 22.0) {
        float rT;
        int rObj;
        vec3 rNorm;
        int rTests;
        traceScene(surfPos + surfNormal * 0.01, reflDir, ignoreId, 1e9, rT, rObj, rNorm, rTests);
        if (rObj >= 0) {
            vec3 rPos = surfPos + reflDir * rT;
            vec3 rL = normalize(uLightPos.xyz - rPos);
            float rDiff = isSphereHit ? (0.22 + 0.78 * max(0.0, dot(rNorm, rL)))
                                      : (0.25 + 0.75 * max(0.0, dot(rNorm, rL)));
            reflCol = uSphereColors[rObj].rgb * rDiff;
        } else if (isSphereHit && reflDir.y < -0.005) {
            float rfT = (-1.25 - surfPos.y) / reflDir.y;
            vec3 rfPos = surfPos + reflDir * rfT;
            float chk = mod(floor(rfPos.x * 0.9) + floor(rfPos.z * 0.9), 2.0);
            reflCol = mix((chk > 0.5) ? vec3(0.20, 0.24, 0.34) : vec3(0.08, 0.10, 0.16), skyCol, clamp(rfT * 0.03, 0.0, 1.0));
        }
    }

    if (isSphereHit) {
        float ndotl = max(0.0, dot(surfNormal, L));
        vec3 halfVec = normalize(L - rd);
        float nh = max(0.0, dot(surfNormal, halfVec));
        float nh2 = nh * nh;
        float nh4 = nh2 * nh2;
        float nh16 = nh4 * nh4;
        float spec = (nh16 * nh16 * nh16) * shadow;

        vec4 mat = uSphereColors[hitObj];
        vec3 baseLit = mat.rgb * (0.18 + 0.82 * ndotl * shadow) + vec3(1.0) * spec * 0.65;
        float om = 1.0 - max(0.0, dot(-rd, surfNormal));
        float om2 = om * om;
        float fresnel = mat.a + (1.0 - mat.a) * (om2 * om2);
        vec3 finalCol = mix(baseLit, reflCol, clamp(fresnel, 0.0, 0.92));
        FragColor = vec4(finalCol, 1.0);
    } else {
        float check = mod(floor(surfPos.x * 0.9) + floor(surfPos.z * 0.9), 2.0);
        vec3 floorAlbedo = (check > 0.5) ? vec3(0.22, 0.26, 0.36) : vec3(0.08, 0.11, 0.18);
        vec3 litFloor = floorAlbedo * (0.22 + 0.78 * shadow);
        vec3 shadedFloor = mix(litFloor, reflCol, 0.26);
        vec3 fogged = mix(shadedFloor, skyCol, clamp(floorT * 0.025, 0.0, 1.0));
        FragColor = vec4(fogged, 1.0);
    }
}

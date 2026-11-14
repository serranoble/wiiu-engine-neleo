// Trazador analítico ejecutado por fragmento. Los 13 uniforms vec4 siguen el orden
// KC2[0..12] que SceneRaytracingGPU carga como 52 floats contiguos.
#version 330 core
in vec2 vUV;
out vec4 FragColor;

uniform vec4 uCamPos;       // xyz = pos, w = 0
uniform vec4 uCamForward;   // xyz = forward, w = 0
uniform vec4 uCamRight;     // xyz = right, w = 0
uniform vec4 uCamUp;        // xyz = up, w = 0
uniform vec4 uParams;       // x = uAspect, y = uTime, zw = 0

uniform vec4 uSphere0;      // xyz = center, w = radius
uniform vec4 uSphere0Color; // rgb = color, a = reflectivity
uniform vec4 uSphere1;      // xyz = center, w = radius
uniform vec4 uSphere1Color; // rgb = color, a = reflectivity
uniform vec4 uBoxCenter;    // xyz = center, w = 0
uniform vec4 uBoxHalf;      // xyz = halfSize, w = 0
uniform vec4 uBoxColor;     // rgb = color, a = reflectivity
uniform vec4 uLightPos;     // xyz = lightPos, w = 0

float sphereIntersect(vec3 ro, vec3 rd, vec3 center, float radius) {
    vec3 oc = ro - center;
    float b = dot(oc, rd);
    float c = dot(oc, oc) - radius * radius;
    float disc = b * b - c;
    if (disc < 0.0) return -1.0;
    float t = -b - sqrt(disc);
    return (t > 0.001) ? t : -1.0;
}

vec2 boxIntersect(vec3 ro, vec3 rd, vec3 center, vec3 halfSize, out vec3 normal) {
    vec3 roLocal = ro - center;
    vec3 invDir = 1.0 / rd;
    vec3 tMin = (-halfSize - roLocal) * invDir;
    vec3 tMax = ( halfSize - roLocal) * invDir;
    vec3 t1 = min(tMin, tMax);
    vec3 t2 = max(tMin, tMax);
    float tNear = max(max(t1.x, t1.y), t1.z);
    float tFar  = min(min(t2.x, t2.y), t2.z);
    if (tFar < tNear || tFar < 0.0) return vec2(-1.0, -1.0);
    
    vec3 absLocal = abs((roLocal + rd * tNear) / halfSize);
    if (absLocal.x >= absLocal.y && absLocal.x >= absLocal.z) {
        normal = vec3(sign(roLocal.x + rd.x * tNear), 0.0, 0.0);
    } else if (absLocal.y >= absLocal.x && absLocal.y >= absLocal.z) {
        normal = vec3(0.0, sign(roLocal.y + rd.y * tNear), 0.0);
    } else {
        normal = vec3(0.0, 0.0, sign(roLocal.z + rd.z * tNear));
    }
    
    return vec2(tNear, tFar);
}

vec4 traceRay(vec2 uv) {
    vec2 ndc = (uv * 2.0 - 1.0);
    ndc.x *= uParams.x;
    
    vec3 rayDir = normalize(uCamRight.xyz * ndc.x + uCamUp.xyz * ndc.y + uCamForward.xyz * 1.4);
    
    float nearestT = 1e9;
    int hitObj = -1;
    vec3 hitNormal = vec3(0.0);
    
    // Fondo cielo cósmico dinámico HD
    vec3 skyColor = mix(vec3(0.03, 0.06, 0.15), vec3(0.10, 0.18, 0.35), clamp(uv.y, 0.0, 1.0));
    vec3 hitColor = skyColor;
    float reflectivity = 0.0;

    // Piso cuadriculado reflectante con perspectiva 3D
    if (rayDir.y < -0.02) {
        float floorT = (-1.15 - uCamPos.y) / rayDir.y;
        if (floorT > 0.0) {
            vec3 floorPos = uCamPos.xyz + rayDir * floorT;
            float check = mod(floor(floorPos.x * 1.5) + floor(floorPos.z * 1.5), 2.0);
            vec3 floorCol = (check > 0.5) ? vec3(0.16, 0.20, 0.30) : vec3(0.07, 0.09, 0.16);
            hitColor = mix(floorCol, skyColor, clamp(floorT * 0.035, 0.0, 1.0));
        }
    }
    
    // Esfera 0 (Cyan Espejo)
    float t0 = sphereIntersect(uCamPos.xyz, rayDir, uSphere0.xyz, uSphere0.w);
    if (t0 > 0.001 && t0 < nearestT) {
        nearestT = t0;
        hitObj = 0;
        vec3 hitPos = uCamPos.xyz + rayDir * t0;
        hitNormal = (hitPos - uSphere0.xyz) / uSphere0.w;
        hitColor = uSphere0Color.rgb;
        reflectivity = uSphere0Color.a;
    }
    
    // Esfera 1 (Dorado Metálico)
    float t1 = sphereIntersect(uCamPos.xyz, rayDir, uSphere1.xyz, uSphere1.w);
    if (t1 > 0.001 && t1 < nearestT) {
        nearestT = t1;
        hitObj = 1;
        vec3 hitPos = uCamPos.xyz + rayDir * t1;
        hitNormal = (hitPos - uSphere1.xyz) / uSphere1.w;
        hitColor = uSphere1Color.rgb;
        reflectivity = uSphere1Color.a;
    }
    
    // Cubo (Magenta)
    vec3 boxNorm;
    vec2 tBox = boxIntersect(uCamPos.xyz, rayDir, uBoxCenter.xyz, uBoxHalf.xyz, boxNorm);
    if (tBox.x > 0.001 && tBox.x < nearestT) {
        nearestT = tBox.x;
        hitObj = 2;
        hitNormal = boxNorm;
        hitColor = uBoxColor.rgb;
        reflectivity = uBoxColor.a;
    }

    if (hitObj >= 0) {
        vec3 hitPos = uCamPos.xyz + rayDir * nearestT;
        vec3 lightDir = normalize(uLightPos.xyz - hitPos);
        
        // Sombras dinámicas en GPU
        vec3 shadowOrigin = hitPos + hitNormal * 0.005;
        bool inShadow = false;
        if (hitObj != 0 && sphereIntersect(shadowOrigin, lightDir, uSphere0.xyz, uSphere0.w) > 0.001) inShadow = true;
        if (!inShadow && hitObj != 1 && sphereIntersect(shadowOrigin, lightDir, uSphere1.xyz, uSphere1.w) > 0.001) inShadow = true;
        
        float diff = inShadow ? 0.25 : max(0.30, dot(hitNormal, lightDir));
        
        // Reflejos especulares en GPU
        vec3 reflectColor = skyColor;
        if (!inShadow && reflectivity > 0.05) {
            vec3 reflectDir = reflect(rayDir, hitNormal);
            float refNearestT = 1e9;
            int refHitObj = -1;
            vec3 refNormal = vec3(0.0);
            
            float r0 = sphereIntersect(shadowOrigin, reflectDir, uSphere0.xyz, uSphere0.w);
            if (hitObj != 0 && r0 > 0.001 && r0 < refNearestT) {
                refNearestT = r0;
                refHitObj = 0;
                refNormal = ((shadowOrigin + reflectDir * r0) - uSphere0.xyz) / uSphere0.w;
            }
            float r1 = sphereIntersect(shadowOrigin, reflectDir, uSphere1.xyz, uSphere1.w);
            if (hitObj != 1 && r1 > 0.001 && r1 < refNearestT) {
                refNearestT = r1;
                refHitObj = 1;
                refNormal = ((shadowOrigin + reflectDir * r1) - uSphere1.xyz) / uSphere1.w;
            }
            
            if (refHitObj >= 0) {
                vec3 refHitPos = shadowOrigin + reflectDir * refNearestT;
                vec3 refLightDir = normalize(uLightPos.xyz - refHitPos);
                float refDiff = max(0.25, dot(refNormal, refLightDir));
                vec3 refObjColor = (refHitObj == 0) ? uSphere0Color.rgb : uSphere1Color.rgb;
                reflectColor = refObjColor * refDiff;
            }
        }
        
        vec3 finalColor = mix(hitColor * diff, reflectColor, reflectivity);
        return vec4(finalColor, 1.0);
    } else {
        return vec4(hitColor, 1.0);
    }
}

void main() {
    // Trazado de rayos primario directo por fragmento (1 rayo por píxel)
    // El anti-aliasing se maneja de forma eficiente mediante escalado bilineal por hardware
    FragColor = traceRay(vUV);
}

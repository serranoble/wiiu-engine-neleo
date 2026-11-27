#version 330 core

in vec3 vWorldPos;
in vec3 vNormal;
in vec2 vTexCoord;
in vec4 vColor;
in vec4 vLightSpacePos;
in vec3 vLocalPos;

uniform sampler2D uTexture;
uniform sampler2D uSkybox;

// Variante especializada para Suite G4 (Modos 4=Vidrio, 5=Agua Física + RTT Planar, 6=Skybox Atlas)
// Solo requiere 6 vec4 (24 floats) de uniformes y bajo conteo de GPRs en AMD Latte R700.
uniform vec4 uPsData[6];

out vec4 fragColor;

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

float FastPow5(float x) {
    float x2 = x * x;
    return x2 * x2 * x;
}

void main() {
    float shadingMode = uPsData[0].w;

    // Modo 6: Skybox Atlas 3x2 infinito
    if (shadingMode >= 5.5) {
        fragColor = vec4(SampleCubeAtlas(normalize(vLocalPos)), 1.0);
        return;
    }

    vec3 camPos = uPsData[0].xyz;
    vec3 V = normalize(camPos - vWorldPos);
    vec3 N = normalize(vNormal);
    vec3 dirL = normalize(uPsData[1].xyz);

    // Modo 5: Agua Física Realista + Reflexión Planar RTT + Cáusticas + Beer-Lambert
    if (shadingMode >= 4.5) {
        float t = uPsData[1].w;
        float amp = max(uPsData[3].w, 0.05);
        vec2 p = vWorldPos.xz;
        float distCam = length(camPos - vWorldPos);
        float detailFade = clamp(1.0 - distCam / 60.0, 0.12, 1.0);

        float w1 = p.x * 0.48 + p.y * 0.28 + t * 1.45;
        float w2 = p.x * -0.36 + p.y * 0.62 + t * 1.15;
        float w3 = p.x * 0.85 - p.y * 0.72 + t * 1.95;

        float dhdx = (cos(w1) * 0.48 * 0.055 + cos(w2) * -0.36 * 0.048 + cos(w3) * 0.85 * 0.026) * amp;
        float dhdz = (cos(w1) * 0.28 * 0.055 + cos(w2) *  0.62 * 0.048 + cos(w3) * -0.72 * 0.026) * amp;
        float crest = sin(w1) * 0.45 + sin(w2) * 0.35 + sin(w3) * 0.20;

        float r1 = p.x * 2.35 + p.y * 1.75 + t * 2.90;
        float r2 = p.x * -2.85 + p.y * 2.40 - t * 2.45;
        float ripScale = amp * detailFade;
        dhdx += (cos(r1) * 2.35 * 0.017 + cos(r2) * -2.85 * 0.015) * ripScale;
        dhdz += (cos(r1) * 1.75 * 0.017 + cos(r2) *  2.40 * 0.015) * ripScale;

        vec3 Nw = normalize(vec3(-dhdx, 1.0, -dhdz));
        float NdotV = clamp(dot(Nw, V), 0.0, 1.0);

        vec3 R = reflect(-V, Nw);
        vec3 skyRefl = SampleCubeAtlas(vec3(R.x, max(R.y, 0.01), R.z));
        vec3 reflCol = skyRefl;
        if (uPsData[4].x > 0.5) {
            vec2 reflNDC = vLightSpacePos.xy / max(vLightSpacePos.w, 0.0001);
            vec2 baseReflUV = vec2(reflNDC.x * 0.5 + 0.5, 0.5 - reflNDC.y * 0.5);
            vec2 clampedUV = clamp(baseReflUV + Nw.xz * (0.068 * detailFade), 0.004, 0.996);
            vec3 planarCol = texture(uTexture, clampedUV).rgb;
            vec2 edgeDist = min(clampedUV, 1.0 - clampedUV);
            float edgeMask = smoothstep(0.004, 0.055, min(edgeDist.x, edgeDist.y));
            reflCol = mix(skyRefl, planarCol, edgeMask);
        }

        vec3 refrDir = refract(-V, Nw, 0.75);
        float radialDist = length(p - vec2(1.0, -1.8));
        float waterDepth = clamp(1.35 + radialDist * 0.14, 1.35, 5.20);
        float tBed = waterDepth / max(-refrDir.y, 0.18);
        vec2 bedXZ = p + refrDir.xz * tBed;

        float sandWave = 0.5 + 0.5 * sin(bedXZ.x * 0.9 + sin(bedXZ.y * 0.55) * 1.4);
        vec3 seabedCol = mix(vec3(0.42, 0.39, 0.30), vec3(0.62, 0.56, 0.42), sandWave);

        float cA = sin(bedXZ.x * 2.1 + bedXZ.y * 1.6 + t * 1.8) * cos(bedXZ.x * 1.5 - bedXZ.y * 2.3 - t * 1.4);
        float cB = sin(bedXZ.x * 3.1 - bedXZ.y * 2.4 - t * 2.2) * cos(bedXZ.x * 2.2 + bedXZ.y * 3.0 + t * 1.7);
        float causticsBase = clamp(0.5 + 0.5 * (cA + cB), 0.0, 1.0);
        float caustics2 = causticsBase * causticsBase;
        float caustics = (caustics2 * caustics2) * exp(-waterDepth * 0.32) * detailFade;
        seabedCol += uPsData[2].rgb * (caustics * 0.72);

        vec3 transmittance = exp(-tBed * vec3(0.44, 0.14, 0.065));
        vec3 underwaterCol = mix(vec3(0.012, 0.075, 0.145), seabedCol, transmittance);

        float fresnel = 0.02 + 0.98 * FastPow5(1.0 - NdotV);
        vec3 col = mix(underwaterCol, reflCol, clamp(fresnel, 0.0, 1.0));

        float sssBase = clamp(dot(V, -dirL + Nw * 0.55), 0.0, 1.0);
        float sss = (sssBase * sssBase * sssBase) * clamp(crest * 0.5 + 0.5, 0.0, 1.0);
        col += vec3(0.03, 0.34, 0.38) * (sss * 0.42 * detailFade);

        vec3 H = normalize(V + dirL);
        float NdotH = max(dot(Nw, H), 0.0);
        float glitter = pow(NdotH, 384.0) * 3.4 * detailFade + pow(NdotH, 56.0) * 0.14;
        col += uPsData[2].rgb * glitter;

        float horizonFade = smoothstep(32.0, 66.0, distCam);
        vec3 horizonSky = SampleCubeAtlas(normalize(vec3(-V.x, max(-V.y, 0.015), -V.z)));
        col = mix(col, horizonSky, horizonFade);

        fragColor = vec4(clamp(col, 0.0, 1.0), 1.0);
        return;
    }

    // Modo 4: Vidrio / Cristal Refractivo + Reflexión Fresnel
    float ior = max(uPsData[3].w, 1.01);
    float ratio = 1.0 / ior;
    vec3 R = reflect(-V, N);
    vec3 T = refract(-V, N, ratio);
    if (dot(T, T) < 0.0001) {
        T = R;
    }

    float f0Scalar = (ior - 1.0) / (ior + 1.0);
    f0Scalar = f0Scalar * f0Scalar;
    float NdotV = clamp(dot(N, V), 0.0, 1.0);
    float fresnel = f0Scalar + (1.0 - f0Scalar) * FastPow5(1.0 - NdotV);

    vec3 reflCol = SampleCubeAtlas(R);
    vec3 refrCol = SampleCubeAtlas(T) * uPsData[3].rgb;
    vec3 col = mix(refrCol, reflCol, clamp(fresnel, 0.0, 1.0));

    vec3 H = normalize(V + dirL);
    float spec = pow(max(dot(N, H), 0.0), 128.0) * 1.2;
    col += uPsData[2].rgb * spec;

    fragColor = vec4(clamp(col, 0.0, 1.0), 1.0);
}

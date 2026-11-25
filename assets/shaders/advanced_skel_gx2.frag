#version 330 core

in vec3 vWorldPos;
in vec3 vNormal;
in vec2 vTexCoord;
in vec4 vColor;

uniform sampler2D uTexture;

uniform vec4 uCamPosTime;      // xyz = camPos, w = time
uniform vec4 uLightDir;        // xyz = lightDir, w = unused
uniform vec4 uLightColor;      // rgb = lightColor, w = shininess
uniform vec4 uMaterialParams;  // x = reflectionIntensity, y = clayMode, z = bumpScale, w = ambient

out vec4 fragColor;

void main() {
    // Albedo: vColor contiene los colores horneados del pájaro (plumaje naranja, pico amarillo, ojos)
    vec4 texColor = texture(uTexture, vTexCoord);
    vec3 albedo = vColor.rgb;
    // Si la textura tiene color válido y vColor es blanco neutro (modelos estándar), usar texColor
    if (texColor.a > 0.1 && (vColor.r > 0.95 && vColor.g > 0.95 && vColor.b > 0.95)) {
        albedo = texColor.rgb;
    }
    // Si clayMode > 0.5 (modo arcilla de escultura), albedo marfil neutro
    if (uMaterialParams.y > 0.5) {
        albedo = vec3(0.85, 0.82, 0.78);
    }

    vec3 camPos = uCamPosTime.xyz;
    vec3 vRaw = camPos - vWorldPos;
    float vLenSq = dot(vRaw, vRaw);
    vec3 viewDir = mix(vec3(0.0, 0.0, 1.0), vRaw * inversesqrt(max(vLenSq, 1e-8)), step(1e-8, vLenSq));

    vec3 lRaw = uLightDir.xyz;
    float lLenSq = dot(lRaw, lRaw);
    vec3 lightDirNorm = mix(vec3(0.0, 1.0, 0.0), lRaw * inversesqrt(max(lLenSq, 1e-8)), step(1e-8, lLenSq));

    float nLenSq = dot(vNormal, vNormal);
    vec3 N = mix(vec3(0.0, 1.0, 0.0), vNormal * inversesqrt(max(nLenSq, 1e-8)), step(1e-8, nLenSq));

    // 1. Key Light con Half-Lambert Diffuse Wrap (evita zonas en sombra negra absoluta)
    float NdotL = dot(N, lightDirNorm);
    float wrapKey = clamp((NdotL + 0.35) / 1.35, 0.0, 1.0);
    vec3 keyDiffuse = uLightColor.rgb * (wrapKey * wrapKey);

    // 2. Fill Light opuesta elevada en 45° para iluminar caras en sombra
    vec3 fillDirRaw = vec3(-lightDirNorm.x, 0.45, -lightDirNorm.z);
    vec3 fillDir = fillDirRaw * inversesqrt(max(dot(fillDirRaw, fillDirRaw), 1e-8));
    float NdotFill = dot(N, fillDir);
    float wrapFill = clamp((NdotFill + 0.30) / 1.30, 0.0, 1.0);
    vec3 fillDiffuse = (uLightColor.rgb * 0.40 + vec3(0.15, 0.18, 0.22)) * (wrapFill * wrapFill * 0.45);

    // 3. Luz ambiental calibrada (gradiente cielo/suelo)
    vec3 ambient = (uLightColor.rgb * 0.30 + vec3(0.12, 0.14, 0.16)) * (N.y * 0.25 + 0.75);

    // 4. Especular Blinn-Phong (sin NaN cuando viewDir == -lightDirNorm)
    vec3 hRaw = lightDirNorm + viewDir;
    float hLenSq = dot(hRaw, hRaw);
    vec3 halfVec = mix(N, hRaw * inversesqrt(max(hLenSq, 1e-8)), step(1e-6, hLenSq));
    float specPower = max(uLightColor.w, 16.0);
    float spec = pow(max(dot(N, halfVec), 0.0), specPower) * 0.40;

    // 5. Rim Light Fresnel desenrollado algebraicamente (0 LOG/EXP en VLIW5)
    float NdotV = clamp(dot(N, viewDir), 0.0, 1.0);
    float oneMinusNdotV = 1.0 - NdotV;
    float r2 = oneMinusNdotV * oneMinusNdotV;
    float rimFactor = r2 * r2;
    vec3 rimLight = (uLightColor.rgb * 0.35 + vec3(0.15, 0.18, 0.22)) * (rimFactor * 0.35);

    vec3 litColor = albedo * (ambient + keyDiffuse + fillDiffuse) + uLightColor.rgb * spec + rimLight;

    // Reflexión ambiental hemisférica analítica con Fresnel de Schlick modulada por máscara Alpha (ojos reflectantes)
    // 0 samplerCube / 0 instrucción CUBE en AMD Latte R700
    if (uMaterialParams.x > 0.01 && texColor.a > 0.01) {
        vec3 R = reflect(-viewDir, N);
        vec3 envColor = mix(vec3(0.18, 0.22, 0.28), vec3(0.55, 0.72, 0.92), clamp(R.y * 0.5 + 0.5, 0.0, 1.0));

        // Para los ojos del pájaro (córnea húmeda/cristalina), elevar F0 a 0.22 para brillo frontal
        float f0 = mix(0.04, 0.22, texColor.a);
        float f5 = rimFactor * oneMinusNdotV;
        float fresnel = f0 + (1.0 - f0) * f5;
        float reflStrength = uMaterialParams.x * fresnel * texColor.a;

        litColor = mix(litColor, envColor, reflStrength);
    }

    fragColor = vec4(litColor, 1.0);
}

// Sombreado por fragmento del agua: color por elevación, Fresnel, espuma y brillo solar.
// Sus ocho uniforms vec4 corresponden a KC2[0..7] en SceneWater.
#version 330 core
in vec3 vWorldPos;
in vec3 vNormal;
out vec4 FragColor;

// Uniforms de Iluminación y Paleta de Color (KC2[0 .. 7])
uniform vec4 uSunDir;            // KC2[0]: xyz = sunDir, w = 0
uniform vec4 uSunColor;          // KC2[1]: rgb = sunColor, a = 1
uniform vec4 uWaterDeepColor;    // KC2[2]: rgb = deepColor, a = 1
uniform vec4 uWaterShallowColor; // KC2[3]: rgb = shallowColor, a = 1
uniform vec4 uWaterFoamColor;    // KC2[4]: rgb = foamColor, a = 1
uniform vec4 uSkyHorizonColor;   // KC2[5]: rgb = skyHorizonColor, a = 1
uniform vec4 uCamPos;            // KC2[6]: xyz = camPos, w = maxAmp
uniform vec4 uShadingParams;     // KC2[7]: x = foamEnabled, y = waterLevel

void main() {
    float waterLevel = uShadingParams.y;
    float maxAmp = max(0.001, uCamPos.w);
    
    // 1. Coloración Cel-shaded por elevación de onda (Estilo Zelda: BotW Lago Hylia)
    float elevRatio = clamp((vWorldPos.y - waterLevel) / maxAmp, -1.0, 1.0);
    float tElev = elevRatio * 0.5 + 0.5; // [0.0 = fondo de valle, 1.0 = cresta máxima]
    vec3 baseWater = mix(uWaterDeepColor.rgb, uWaterShallowColor.rgb, tElev);

    // 2. Normal por fragmento y vector de visión
    vec3 normal = normalize(vNormal);
    vec3 viewDir = normalize(uCamPos.xyz - vWorldPos);

    // 3. Factor de Fresnel de Schlick (Reflejo del cielo en ángulos rasantes)
    float NdotV = clamp(dot(normal, viewDir), 0.0, 1.0);
    float oneMinusNV = 1.0 - NdotV;
    float fresnel = 0.05 + 0.95 * (oneMinusNV * oneMinusNV * oneMinusNV * oneMinusNV);
    vec3 color = mix(baseWater, uSkyHorizonColor.rgb, fresnel * 0.5);

    // 4. Espuma procedural en crestas afiladas (Crest Foam de BotW)
    if (uShadingParams.x > 0.5) {
        float slopeFactor = 1.0 - normal.y;
        if (tElev > 0.60 || slopeFactor > 0.30) {
            float elevFoam = (tElev - 0.60) / 0.35;
            float slopeFoam = (slopeFactor - 0.30) / 0.35;
            float foamAmount = clamp(max(elevFoam, slopeFoam), 0.0, 1.0);
            color = mix(color, uWaterFoamColor.rgb, foamAmount * 0.85);
        }
    }

    // 5. Destello solar especular Blinn-Phong en facetas del agua
    vec3 halfDir = normalize(uSunDir.xyz + viewDir);
    float NdotH = max(0.0, dot(normal, halfDir));
    if (NdotH > 0.65) {
        float s2 = NdotH * NdotH;
        float s4 = s2 * s2;
        float s8 = s4 * s4;
        float s16 = s8 * s8;
        float s32 = s16 * s16;
        float spec = s32 * s4 * 1.8;
        color += uSunColor.rgb * spec;
    }

    FragColor = vec4(clamp(color, 0.0, 1.0), 1.0);
}

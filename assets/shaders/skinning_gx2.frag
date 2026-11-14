#version 330 core

in vec3 vWorldPos;
in vec3 vNormal;
in vec2 vTexCoord;
in vec4 vColor;

uniform vec4 uCamPos;       // KC2[0]: xyz = camPos, w = unused
uniform vec4 uLightDir;     // KC2[1]: xyz = lightDir, w = intensity
uniform vec4 uLightColor;   // KC2[2]: rgb = lightColor, w = roughness
uniform vec4 uMaterialMode; // KC2[3]: x = mode (0: shaded, 1: bone heat, 2: clay), y = uvTiling

out vec4 fragColor;

void main() {
    vec3 N = normalize(vNormal);
    vec3 L = normalize(uLightDir.xyz);
    vec3 V = normalize(uCamPos.xyz - vWorldPos);

    // 1. Key Light con Half-Lambert Diffuse Wrap (OPT-322)
    float NdotL = dot(N, L);
    float wrapKey = clamp((NdotL + 0.35) / 1.35, 0.0, 1.0);
    vec3 keyDiffuse = uLightColor.rgb * (wrapKey * wrapKey);

    // 2. Fill Light opuesta elevada en 45°
    vec3 fillDir = normalize(vec3(-L.x, 0.45, -L.z));
    float NdotFill = dot(N, fillDir);
    float wrapFill = clamp((NdotFill + 0.30) / 1.30, 0.0, 1.0);
    vec3 fillDiffuse = (uLightColor.rgb * 0.40 + vec3(0.12, 0.14, 0.18)) * (wrapFill * wrapFill * 0.45);

    // 3. Rim Light Fresnel de Silueta
    float NdotV = clamp(dot(N, V), 0.0, 1.0);
    float rimFactor = pow(1.0 - NdotV, 4.0);
    vec3 rimLight = (uLightColor.rgb * 0.35 + vec3(0.15, 0.18, 0.22)) * (rimFactor * 0.35);

    // 4. Luz ambiental de estudio balanceada
    vec3 ambient = (uLightColor.rgb * 0.25 + vec3(0.12, 0.14, 0.16)) * (N.y * 0.25 + 0.75);

    // 5. Especular Blinn-Phong
    vec3 H = normalize(L + V);
    float spec = pow(max(dot(N, H), 0.0), 32.0) * (1.0 - uLightColor.w) * 0.45;

    vec3 albedo = vColor.rgb;
    if (uMaterialMode.x > 1.5) {
        albedo = vec3(0.85, 0.83, 0.80); // Modo arcilla de estudio (Clay)
    }

    vec3 finalLit = albedo * (ambient + keyDiffuse + fillDiffuse) + uLightColor.rgb * spec + rimLight;
    fragColor = vec4(finalLit, vColor.a);
}

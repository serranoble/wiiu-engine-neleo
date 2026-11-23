#version 330 core

in vec3 vWorldPos;
in vec3 vNormal;
in vec2 vTexCoord;
in vec4 vColor;

uniform sampler2D uTexture;

uniform vec4 uCamPosTime;      // xyz = camPos, w = time
uniform vec4 uLightDir;        // xyz = lightDir, w = unused
uniform vec4 uLightColor;      // rgb = lightColor, w = shininess
uniform vec4 uMaterialParams;  // x = reflectionIntensity, y = normalMapEnabled, z = bumpScale, w = ambient

out vec4 fragColor;

void main() {
    vec4 texColor = texture(uTexture, vTexCoord);
    float useVertexColor = step(0.05, vColor.a) * step(dot(texColor.rgb, vec3(1.0)), 0.01);
    vec3 albedo = mix(texColor.rgb, vColor.rgb, useVertexColor);

    vec3 viewDir = normalize(uCamPosTime.xyz - vWorldPos);
    vec3 lightDirNorm = normalize(uLightDir.xyz);
    vec3 N = normalize(vNormal);

    float NdotL = dot(N, lightDirNorm);
    float wrapKey = clamp((NdotL + 0.25) / 1.25, 0.0, 1.0);
    vec3 keyDiffuse = uLightColor.rgb * (wrapKey * wrapKey);

    vec3 fillDir = normalize(vec3(-lightDirNorm.x, 0.40, -lightDirNorm.z));
    float wrapFill = clamp((dot(N, fillDir) + 0.25) / 1.25, 0.0, 1.0);
    vec3 fillDiffuse = (uLightColor.rgb * 0.35 + vec3(0.12, 0.15, 0.18)) * (wrapFill * wrapFill * 0.40);

    vec3 ambient = uLightColor.rgb * (uMaterialParams.w * (N.y * 0.25 + 0.75));

    vec3 halfVec = normalize(lightDirNorm + viewDir);
    float specBase = max(dot(N, halfVec), 0.0);
    float spec2 = specBase * specBase;
    float spec4 = spec2 * spec2;
    float spec16 = (spec4 * spec4) * (spec4 * spec4);
    float spec = spec16 * spec16 * 0.30;

    vec3 litColor = albedo * (ambient + keyDiffuse + fillDiffuse) + uLightColor.rgb * spec;

    // Reflexión hemisférica analítica 100% branchless (0 samplerCube / 0 instrucción CUBE en AMD Latte R700)
    vec3 R = reflect(-viewDir, N);
    vec3 envColor = mix(vec3(0.18, 0.22, 0.28), vec3(0.55, 0.72, 0.92), clamp(R.y * 0.5 + 0.5, 0.0, 1.0));
    float oneMinusNdotV = 1.0 - clamp(dot(N, viewDir), 0.0, 1.0);
    float f2 = oneMinusNdotV * oneMinusNdotV;
    float f5 = f2 * f2 * oneMinusNdotV;
    float f0 = mix(0.04, 0.25, texColor.a);
    float fresnel = f0 + (1.0 - f0) * f5;
    float reflStrength = clamp(uMaterialParams.x * fresnel * texColor.a, 0.0, 1.0);

    litColor = mix(litColor, envColor, reflStrength);
    fragColor = vec4(litColor, 1.0);
}

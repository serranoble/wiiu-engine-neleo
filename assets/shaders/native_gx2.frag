// Pixel stage Blinn-Phong; se compila junto con native_gx2.vert en native_gx2.gsh.
// Su layout de uniforms debe coincidir con GX2SetPixelUniformReg en el código de escena.
#version 330 core
in vec3 vWorldPos;
in vec3 vNormal;
in vec4 vColor;

out vec4 FragColor;

uniform vec4 uLightDir;
uniform vec4 uLightColor;
uniform vec4 uCamPos;
uniform vec4 uAmbient;

void main() {
    vec3 N = normalize(vNormal);
    vec3 L = normalize(uLightDir.xyz);
    vec3 V = normalize(uCamPos.xyz - vWorldPos);
    vec3 H = normalize(L + V);

    float NdotL = max(dot(N, L), 0.0);
    
    float NdotH = max(dot(N, H), 0.0);
    float s2 = NdotH * NdotH;
    float s4 = s2 * s2;
    float s8 = s4 * s4;
    float s16 = s8 * s8;
    float s32 = s16 * s16;
    float specFactor = s32 * s4;
    
    float NdotV = clamp(dot(N, V), 0.0, 1.0);
    float oneMinusNV = 1.0 - NdotV;
    float fresnel = 0.04 + 0.96 * (oneMinusNV * oneMinusNV * oneMinusNV);

    vec3 ambient = uAmbient.rgb * vColor.rgb;
    vec3 diffuse = NdotL * uLightColor.rgb * vColor.rgb * uLightDir.w;
    vec3 specular = specFactor * uLightColor.rgb * (uAmbient.w + fresnel * 0.5);

    vec3 finalColor = ambient + diffuse + specular;
    FragColor = vec4(clamp(finalColor, 0.0, 1.0), vColor.a);
}

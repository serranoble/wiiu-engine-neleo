#version 330 core

in vec3 vWorldPos;
in vec3 vNormal;
in vec2 vTexCoord;
in vec4 vColor;

uniform sampler2D uTexture;
uniform samplerCube uSkybox;

uniform vec4 uCamPosTime;      // xyz = camPos, w = time
uniform vec4 uLightDir;        // xyz = lightDir, w = unused
uniform vec4 uLightColor;      // rgb = lightColor, w = shininess (e.g. 32.0)
uniform vec4 uMaterialParams;  // x = reflectionIntensity (0.0..1.0), y = normalMapEnabled (0.0/1.0), z = bumpScale (e.g. 2.0), w = ambient (e.g. 0.35)

out vec4 fragColor;

void main() {
    vec4 texColor = texture(uTexture, vTexCoord);
    vec3 albedo = (vColor.a > 0.05 && dot(texColor.rgb, vec3(1.0)) < 0.01) ? vColor.rgb : texColor.rgb;

    vec3 camPos = uCamPosTime.xyz;
    vec3 viewDir = normalize(camPos - vWorldPos);
    vec3 lightDirNorm = normalize(uLightDir.xyz);

    vec3 N = normalize(vNormal);

    // Factor de superficie vítrea/reflectante modulado por máscara Alpha
    float isGlass = clamp((texColor.a - 0.05) / 0.5, 0.0, 1.0);

    // Perturbación de normales en espacio tangente mediante derivadas analíticas en pantalla (Gordon & Clevenger Cap. 10)
    // Se atenúa en zonas de cristal (1.0 - isGlass) para mantener la superficie perfectamente plana y evitar distorsión radial
    if (uMaterialParams.y > 0.5) {
        vec3 dp1 = dFdx(vWorldPos);
        vec3 dp2 = dFdy(vWorldPos);
        vec2 duv1 = dFdx(vTexCoord);
        vec2 duv2 = dFdy(vTexCoord);

        vec3 dp2perp = cross(dp2, N);
        vec3 dp1perp = cross(N, dp1);
        vec3 T = dp2perp * duv1.x + dp1perp * duv2.x;
        vec3 B = dp2perp * duv1.y + dp1perp * duv2.y;

        float invmax = inversesqrt(max(max(dot(T, T), dot(B, B)), 1e-8));
        mat3 TBN = mat3(T * invmax, B * invmax, N);

        // Muestreo de altura por gradiente de luminancia para relieve detallado
        vec2 dUV = vec2(0.00048828125, 0.00048828125); // 1/2048
        float hC = dot(texColor.rgb, vec3(0.299, 0.587, 0.114));
        float hR = dot(texture(uTexture, vTexCoord + vec2(dUV.x, 0.0)).rgb, vec3(0.299, 0.587, 0.114));
        float hU = dot(texture(uTexture, vTexCoord + vec2(0.0, dUV.y)).rgb, vec3(0.299, 0.587, 0.114));

        float bumpFactor = uMaterialParams.z * (1.0 - isGlass);
        vec3 tanPerturb = normalize(vec3((hC - hR) * bumpFactor, (hC - hU) * bumpFactor, 1.0));
        N = normalize(TBN * tanPerturb);
    }

    // 1. Key Light con Half-Lambert Diffuse Wrap (Gordon & Clevenger Cap. 7 & 8)
    float NdotL = dot(N, lightDirNorm);
    float wrapKey = clamp((NdotL + 0.25) / 1.25, 0.0, 1.0);
    vec3 keyDiffuse = uLightColor.rgb * (wrapKey * wrapKey);

    // 2. Fill Light opuesta para suavizar sombras duras
    vec3 fillDir = normalize(vec3(-lightDirNorm.x, 0.40, -lightDirNorm.z));
    float NdotFill = dot(N, fillDir);
    float wrapFill = clamp((NdotFill + 0.25) / 1.25, 0.0, 1.0);
    vec3 fillDiffuse = (uLightColor.rgb * 0.35 + vec3(0.12, 0.15, 0.18)) * (wrapFill * wrapFill * 0.40);

    // 3. Luz ambiental calibrada
    vec3 ambient = uLightColor.rgb * (uMaterialParams.w * (N.y * 0.25 + 0.75));

    // 4. Especular Blinn-Phong
    vec3 halfVec = normalize(lightDirNorm + viewDir);
    float specPower = max(uLightColor.w, 16.0);
    float spec = pow(max(dot(N, halfVec), 0.0), specPower) * 0.30;

    vec3 litColor = albedo * (ambient + keyDiffuse + fillDiffuse) + uLightColor.rgb * spec;

    // Reflexión ambiental CubeMap con Fresnel de Schlick modulada por máscara Alpha (ventanas de cristal)
    if (uMaterialParams.x > 0.01 && texColor.a > 0.01) {
        vec3 R = reflect(-viewDir, N);
        vec3 envColor = texture(uSkybox, R).rgb;

        float NdotV = clamp(dot(N, viewDir), 0.0, 1.0);
        // Para cristal de ventanas (texColor.a alto), F0 base incrementado a 0.20 para brillo frontal nítido
        float f0 = mix(0.04, 0.25, texColor.a);
        float fresnel = f0 + (1.0 - f0) * pow(1.0 - NdotV, 5.0);
        float reflStrength = uMaterialParams.x * fresnel * texColor.a;

        litColor = mix(litColor, envColor, reflStrength);
    }

    fragColor = vec4(litColor, 1.0);
}

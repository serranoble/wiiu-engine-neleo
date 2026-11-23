#version 330 core

in vec2 vUV;

uniform sampler2D uSceneHDR;
uniform sampler2D uBloomTex;

// Empaquetado contiguo de 4 vec4 (16 floats) para offsets exactos en CafeGLSL register mode:
// [0] = (passMode: 0=BrightPass, 1=Kawase2D, 3=Composite; toneMapMode: 0=OFF, 1=Reinhard, 2=ACES; exposure; bloomIntensity)
// [1] = (texelSizeX, texelSizeY, bloomThreshold, gammaEnabled)
// [2] = (vignetteStrength, bloomEnabled, kawaseOffset, 0.0)
// [3] = reservado
uniform vec4 uPostData[4];

out vec4 fragColor;

vec3 ToneMapACES(vec3 x) {
    const float a = 2.51;
    const float b = 0.03;
    const float c = 2.43;
    const float d = 0.59;
    const float e = 0.14;
    return clamp((x * (a * x + b)) / (x * (c * x + d) + e), 0.0, 1.0);
}

vec3 ToneMapReinhard(vec3 x) {
    return x / (x + vec3(1.0));
}

void main() {
    float passMode = uPostData[0].x;
    vec2 texel = uPostData[1].xy;

    // Pase 0: Bright-Pass (extracción HDR con soft knee para Bloom, acotado contra Inf/NaN en RGBA16F)
    if (passMode < 0.5) {
        vec3 hdr = clamp(texture(uSceneHDR, vUV).rgb, vec3(0.0), vec3(16.0));
        float threshold = uPostData[1].z;
        float lum = dot(hdr, vec3(0.2126, 0.7152, 0.0722));
        float knee = max(threshold * 0.45, 0.001);
        float soft = clamp((lum - threshold + knee) / (2.0 * knee), 0.0, 1.0);
        float contrib = max(lum - threshold, 0.0) + soft * soft * knee;
        float factor = contrib / max(lum, 0.0001);
        fragColor = vec4(clamp(hdr * clamp(factor, 0.0, 4.0), vec3(0.0), vec3(16.0)), 1.0);
        return;
    }

    // Pase 1: Blur 2D Isotrópico Kawase 5-tap (1 sola cláusula TEX 5, cero presión de registros, halo 100% circular)
    if (passMode < 2.5) {
        vec2 off = texel * max(uPostData[2].z, 1.0);
        vec3 c0 = clamp(texture(uSceneHDR, vUV).rgb, vec3(0.0), vec3(8.0));
        vec3 c1 = clamp(texture(uSceneHDR, clamp(vUV + vec2(-off.x, -off.y), 0.002, 0.998)).rgb, vec3(0.0), vec3(8.0));
        vec3 c2 = clamp(texture(uSceneHDR, clamp(vUV + vec2( off.x, -off.y), 0.002, 0.998)).rgb, vec3(0.0), vec3(8.0));
        vec3 c3 = clamp(texture(uSceneHDR, clamp(vUV + vec2(-off.x,  off.y), 0.002, 0.998)).rgb, vec3(0.0), vec3(8.0));
        vec3 c4 = clamp(texture(uSceneHDR, clamp(vUV + vec2( off.x,  off.y), 0.002, 0.998)).rgb, vec3(0.0), vec3(8.0));
        vec3 sum = c0 * 0.28 + (c1 + c2 + c3 + c4) * 0.18;
        fragColor = vec4(clamp(sum, vec3(0.0), vec3(8.0)), 1.0);
        return;
    }

    // Pase 3: Composite HDR + Bloom + Exposición + Tone Mapping (ACES / Reinhard / Clamp) + Vignette + Gamma sRGB
    vec3 hdr = texture(uSceneHDR, vUV).rgb;
    vec3 bloom = texture(uBloomTex, vUV).rgb;
    float bloomEnabled = uPostData[2].y;
    float bloomIntensity = uPostData[0].w;
    vec3 combined = hdr + bloom * (bloomIntensity * bloomEnabled);

    float exposure = max(uPostData[0].z, 0.05);
    vec3 exposed = combined * exposure;

    float toneMode = uPostData[0].y;
    vec3 mapped;
    if (toneMode > 1.5) {
        mapped = ToneMapACES(exposed);
    } else if (toneMode > 0.5) {
        mapped = ToneMapReinhard(exposed);
    } else {
        mapped = clamp(exposed, 0.0, 1.0);
    }

    // Vignette sutil cinematográfica
    float vigStrength = uPostData[2].x;
    if (vigStrength > 0.001) {
        vec2 centered = vUV - vec2(0.5);
        float vig = 1.0 - dot(centered, centered) * vigStrength;
        mapped *= clamp(vig, 0.35, 1.0);
    }

    // Corrección Gamma sRGB conmutable (RTR4 Cap. 5.6)
    float gammaEnabled = uPostData[1].w;
    if (gammaEnabled > 0.5) {
        mapped = pow(clamp(mapped, 0.0, 1.0), vec3(1.0 / 2.2));
    }

    fragColor = vec4(clamp(mapped, 0.0, 1.0), 1.0);
}

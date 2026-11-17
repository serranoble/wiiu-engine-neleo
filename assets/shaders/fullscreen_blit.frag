#version 330 core
in vec2 vUV;
out vec4 FragColor;

uniform vec4 uBlitUVTransform; // xy = uvOffset (0, resH/texH en GX2), zw = uvScale (resW/texW, -resH/texH en GX2)
uniform vec4 uBlitUVClamp;     // xy = 0.5 / texSize, zw = (srcSize - 0.5) / texSize
uniform sampler2D uSourceTex;

void main() {
    vec2 uv = clamp(uBlitUVTransform.xy + vUV * uBlitUVTransform.zw,
                    uBlitUVClamp.xy, uBlitUVClamp.zw);
    FragColor = texture(uSourceTex, uv);
}

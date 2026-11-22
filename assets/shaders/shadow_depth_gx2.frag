#version 330 core

out vec4 fragColor;

void main() {
    // Depth-only / Early-Z fallback PS (en silicio se usa Null PS con GX2SetPixelShader(nullptr) y color writes OFF)
    fragColor = vec4(gl_FragCoord.z, 0.0, 0.0, 1.0);
}

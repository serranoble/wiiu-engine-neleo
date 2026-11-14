#version 330 core

in vec2 vTexCoord;

uniform sampler2D uSkyTexture;

out vec4 fragColor;

void main() {
    fragColor = texture(uSkyTexture, vTexCoord);
}

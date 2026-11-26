#version 330 core

in vec2 vTexCoord;
in vec4 vColor;

uniform sampler2D uFontAtlas;

out vec4 FragColor;

void main() {
    vec4 tex = texture(uFontAtlas, vTexCoord);
    FragColor = tex * vColor;
}

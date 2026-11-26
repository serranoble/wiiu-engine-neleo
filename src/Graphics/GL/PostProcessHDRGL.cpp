#include "Graphics/PostProcessHDRGX2.hpp"
#include "Graphics/GL/GLFunctions.hpp"
#include "Core/AssetManager.hpp"
#include <algorithm>
#include <cstdint>
#include <cstring>

namespace Neleo::Graphics {

namespace {

constexpr const char* kFullscreenVS = R"(
#version 330 core
out vec2 vUV;
void main() {
    vec2 pos = vec2((gl_VertexID == 1) ? 3.0 : -1.0,
                    (gl_VertexID == 2) ? 3.0 : -1.0);
    vUV = pos * 0.5 + 0.5;
    gl_Position = vec4(pos, 0.0, 1.0);
}
)";

constexpr const char* kPostProcessFS = R"(
#version 330 core
in vec2 vUV;

uniform sampler2D uSceneHDR;
uniform sampler2D uBloomTex;
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

    float vigStrength = uPostData[2].x;
    if (vigStrength > 0.001) {
        vec2 centered = vUV - vec2(0.5);
        float vig = 1.0 - dot(centered, centered) * vigStrength;
        mapped *= clamp(vig, 0.35, 1.0);
    }

    float gammaEnabled = uPostData[1].w;
    if (gammaEnabled > 0.5) {
        mapped = pow(clamp(mapped, 0.0, 1.0), vec3(1.0 / 2.2));
    }

    fragColor = vec4(clamp(mapped, 0.0, 1.0), 1.0);
}
)";

GL::GLuint CompilePostShader(GL::GLenum type, const char* src) noexcept {
    const auto& gl = GL::GetFunctions();
    const GL::GLuint sh = gl.CreateShader(type);
    if (sh == 0u) return 0u;
    gl.ShaderSource(sh, 1, &src, nullptr);
    gl.CompileShader(sh);
    GL::GLint ok = 0;
    gl.GetShaderiv(sh, GL::GL_COMPILE_STATUS_VAL, &ok);
    if (ok == 0) {
        gl.DeleteShader(sh);
        return 0u;
    }
    return sh;
}

} // namespace

bool PostProcessHDRGX2::Initialize(uint32_t width, uint32_t height) {
    if (m_initialized && m_width == width && m_height == height) {
        return true;
    }
    Shutdown();

    if (width < 64u || height < 64u || width > 1920u || height > 1080u) {
        return false;
    }
    if (!GL::IsLoaded() && !GL::LoadFunctions()) {
        return false;
    }

    m_width = width;
    m_height = height;

    const uint32_t rawBloomW = (width >= 256u) ? (width / 4u) : 64u;
    const uint32_t rawBloomH = (height >= 256u) ? (height / 4u) : 64u;
    const uint32_t bloomW = (rawBloomW + 31u) & ~31u;
    const uint32_t bloomH = (rawBloomH + 31u) & ~31u;

    if (!m_hdrRT.Initialize(m_width, m_height, "PostFX_HDR_RT", RenderTargetFormat::RGBA16_Float, true) ||
        !m_bloomPingRT.Initialize(bloomW, bloomH, "PostFX_BloomPing_RT", RenderTargetFormat::RGBA16_Float, false) ||
        !m_bloomPongRT.Initialize(bloomW, bloomH, "PostFX_BloomPong_RT", RenderTargetFormat::RGBA16_Float, false) ||
        !InitShaderAndQuad()) {
        Shutdown();
        return false;
    }

    m_initialized = true;
    return true;
}

bool PostProcessHDRGX2::InitShaderAndQuad() {
    if (!GL::IsLoaded()) return false;
    const auto& gl = GL::GetFunctions();
    if (!gl.CreateProgram || !gl.GenVertexArrays) return false;

    const GL::GLuint vs = CompilePostShader(GL::GL_VERTEX_SHADER_VAL, kFullscreenVS);
    const GL::GLuint fs = CompilePostShader(GL::GL_FRAGMENT_SHADER_VAL, kPostProcessFS);
    if (vs == 0u || fs == 0u) {
        if (vs != 0u) gl.DeleteShader(vs);
        if (fs != 0u) gl.DeleteShader(fs);
        return false;
    }

    const GL::GLuint prog = gl.CreateProgram();
    if (prog == 0u) {
        gl.DeleteShader(vs);
        gl.DeleteShader(fs);
        return false;
    }
    gl.AttachShader(prog, vs);
    gl.AttachShader(prog, fs);
    gl.LinkProgram(prog);
    gl.DeleteShader(vs);
    gl.DeleteShader(fs);

    GL::GLint linked = 0;
    gl.GetProgramiv(prog, GL::GL_LINK_STATUS_VAL, &linked);
    if (linked == 0) {
        gl.DeleteProgram(prog);
        return false;
    }

    GL::GLuint vao = 0u;
    gl.GenVertexArrays(1, &vao);
    if (vao == 0u) {
        gl.DeleteProgram(prog);
        return false;
    }

    m_postProgram = prog;
    m_fullscreenVao = vao;
    m_sceneTexLoc = gl.GetUniformLocation(prog, "uSceneHDR");
    m_bloomTexLoc = gl.GetUniformLocation(prog, "uBloomTex");
    m_postDataLoc = gl.GetUniformLocation(prog, "uPostData");
    m_sceneTexSlot = 0;
    m_bloomTexSlot = 1;
    m_hasReflectedSamplers = (m_sceneTexLoc >= 0) && (m_bloomTexLoc >= 0) && (m_postDataLoc >= 0);
    return true;
}

void PostProcessHDRGX2::Shutdown() {
    if (GL::IsLoaded()) {
        const auto& gl = GL::GetFunctions();
        if (m_fullscreenVao != 0u && gl.DeleteVertexArrays) {
            const GL::GLuint id = m_fullscreenVao;
            gl.DeleteVertexArrays(1, &id);
        }
        if (m_postProgram != 0u && gl.DeleteProgram) {
            gl.DeleteProgram(m_postProgram);
        }
    }
    m_postProgram = 0u;
    m_fullscreenVao = 0u;
    m_sceneTexLoc = -1;
    m_bloomTexLoc = -1;
    m_postDataLoc = -1;
    m_bloomPongRT.Shutdown();
    m_bloomPingRT.Shutdown();
    m_hdrRT.Shutdown();
    m_hasReflectedSamplers = false;
    m_sceneTexSlot = -1;
    m_bloomTexSlot = -1;
    m_initialized = false;
}

bool PostProcessHDRGX2::BeginHDRScenePass(float clearR, float clearG, float clearB, float clearA) {
    if (!m_initialized) return false;
    return m_hdrRT.BeginPass(clearR, clearG, clearB, clearA, 1.0f);
}

void PostProcessHDRGX2::EndHDRScenePassAndCompose(const PostProcessHDRSettings& settings,
                                                  void* restoreContext,
                                                  void* restoreColor,
                                                  void* restoreDepth,
                                                  uint32_t restoreWidth,
                                                  uint32_t restoreHeight) {
    if (!m_initialized || !GL::IsLoaded() || m_postProgram == 0u || m_fullscreenVao == 0u) return;

    m_hdrRT.FlushForSampling();
    const auto& gl = GL::GetFunctions();

    const float bloomTexelX = 1.0f / static_cast<float>(std::max(1u, m_bloomPingRT.GetWidth()));
    const float bloomTexelY = 1.0f / static_cast<float>(std::max(1u, m_bloomPingRT.GetHeight()));

    float postData[16]{};
    postData[1]  = static_cast<float>(settings.toneMap);
    postData[2]  = settings.exposure;
    postData[3]  = settings.bloomIntensity;
    postData[4]  = bloomTexelX;
    postData[5]  = bloomTexelY;
    postData[6]  = settings.bloomThreshold;
    postData[7]  = settings.gammaEnabled ? 1.0f : 0.0f;
    postData[8]  = settings.vignetteStrength;
    postData[9]  = settings.bloomEnabled ? 1.0f : 0.0f;

    auto drawFullscreen = [&](const GX2Texture* sceneTex, const GX2Texture* bloomTex) noexcept {
        gl.UseProgram(m_postProgram);
        gl.Uniform4fv(m_postDataLoc, 4, postData);
        gl.Uniform1i(m_sceneTexLoc, 0);
        gl.Uniform1i(m_bloomTexLoc, 1);

        gl.ActiveTexture(GL::GL_TEXTURE0_VAL);
        gl.BindTexture(GL::GL_TEXTURE_2D_VAL, sceneTex ? sceneTex->glTextureId : 0u);
        gl.ActiveTexture(GL::GL_TEXTURE1_VAL);
        gl.BindTexture(GL::GL_TEXTURE_2D_VAL, bloomTex ? bloomTex->glTextureId : 0u);
        gl.ActiveTexture(GL::GL_TEXTURE0_VAL);

        gl.Disable(GL::GL_DEPTH_TEST_VAL);
        gl.DepthMask(GL::GL_FALSE_VAL);
        gl.Disable(GL::GL_CULL_FACE_VAL);
        gl.Disable(GL::GL_BLEND_VAL);

        gl.BindVertexArray(m_fullscreenVao);
        gl.DrawArrays(GL::GL_TRIANGLES_VAL, 0, 3);
    };

    if (settings.bloomEnabled) {
        if (m_bloomPingRT.BeginPass(0.0f, 0.0f, 0.0f, 1.0f)) {
            postData[0] = 0.0f;
            drawFullscreen(m_hdrRT.GetColorTexture(), m_hdrRT.GetColorTexture());
            m_bloomPingRT.FlushForSampling();
        }
        if (m_bloomPongRT.BeginPass(0.0f, 0.0f, 0.0f, 1.0f)) {
            postData[0] = 1.0f;
            postData[10] = 1.5f;
            drawFullscreen(m_bloomPingRT.GetColorTexture(), m_bloomPingRT.GetColorTexture());
            m_bloomPongRT.FlushForSampling();
        }
        if (m_bloomPingRT.BeginPass(0.0f, 0.0f, 0.0f, 1.0f)) {
            postData[0] = 1.0f;
            postData[10] = 2.75f;
            drawFullscreen(m_bloomPongRT.GetColorTexture(), m_bloomPongRT.GetColorTexture());
            m_bloomPingRT.FlushForSampling();
        }
    }

    m_hdrRT.EndPass(restoreContext, restoreColor, restoreDepth, restoreWidth, restoreHeight);

    postData[0] = 3.0f;
    const GX2Texture* bloomTex = settings.bloomEnabled ? m_bloomPingRT.GetColorTexture() : m_hdrRT.GetColorTexture();
    drawFullscreen(m_hdrRT.GetColorTexture(), bloomTex);

    gl.ActiveTexture(GL::GL_TEXTURE1_VAL);
    gl.BindTexture(GL::GL_TEXTURE_2D_VAL, 0u);
    gl.ActiveTexture(GL::GL_TEXTURE0_VAL);
    gl.BindTexture(GL::GL_TEXTURE_2D_VAL, 0u);
    gl.BindVertexArray(0u);
    gl.UseProgram(0u);
    gl.DepthMask(GL::GL_TRUE_VAL);
}

} // namespace Neleo::Graphics

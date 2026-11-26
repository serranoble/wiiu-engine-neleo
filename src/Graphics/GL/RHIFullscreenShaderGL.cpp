#include "Graphics/RHI/RHIFullscreenShader.hpp"
#include "Graphics/GL/GLFunctions.hpp"
#include "Core/AssetManager.hpp"
#include <algorithm>
#include <cstring>
#include <string>
#include <vector>

namespace Neleo {

namespace {

using namespace Graphics::GL;

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

constexpr const char* kFallbackBlitFS = R"(
#version 330 core
in vec2 vUV;
out vec4 FragColor;
void main() {
    FragColor = vec4(vUV.x * 0.15, vUV.y * 0.25, 0.35, 1.0);
}
)";

std::string MapGshPathToFragPath(const char* shaderAssetPath) {
    if (!shaderAssetPath) return {};
    std::string p(shaderAssetPath);
    const size_t dot = p.rfind('.');
    if (dot != std::string::npos && p.compare(dot, 4, ".gsh") == 0) {
        p.replace(dot, 4, ".frag");
    }
    return p;
}

GLuint CompileGLShader(GLenum type, const char* src) noexcept {
    const auto& gl = GetFunctions();
    const GLuint sh = gl.CreateShader(type);
    if (sh == 0u) return 0u;
    gl.ShaderSource(sh, 1, &src, nullptr);
    gl.CompileShader(sh);
    GLint ok = 0;
    gl.GetShaderiv(sh, GL_COMPILE_STATUS_VAL, &ok);
    if (ok == 0) {
        gl.DeleteShader(sh);
        return 0u;
    }
    return sh;
}

} // namespace

RHIFullscreenShader::OutputTargetBinding RHIFullscreenShader::s_frameTarget{};

void RHIFullscreenShader::SetActiveFrameTargets(void* colorBuffer, void* depthBuffer) noexcept {
    s_frameTarget.colorBuffer = colorBuffer;
    s_frameTarget.depthBuffer = depthBuffer;
}

void RHIFullscreenShader::ClearActiveFrameTargets() noexcept {
    s_frameTarget = OutputTargetBinding{};
}

void RHIFullscreenShader::SetOutputTarget(const OutputTargetBinding& target) noexcept {
    m_outputTarget = target;
}

void RHIFullscreenShader::ClearOutputTarget() noexcept {
    m_outputTarget = OutputTargetBinding{};
}

bool RHIFullscreenShader::PreloadBinary(const char* shaderAssetPath) {
    const std::string fragPath = MapGshPathToFragPath(shaderAssetPath);
    if (fragPath.empty()) return false;
    return AssetManager::LoadBinaryFile(fragPath, m_stagedBinary) && !m_stagedBinary.empty();
}

bool RHIFullscreenShader::Initialize(const char* shaderAssetPath,
                                     const uint8_t* fallbackData,
                                     size_t fallbackSize) {
    (void)fallbackData;
    (void)fallbackSize;
    Shutdown();

    if (!IsLoaded() && !LoadFunctions()) {
        return false;
    }

    const auto& gl = GetFunctions();
    if (!gl.CreateProgram || !gl.GenVertexArrays) return false;

    std::string fragSrc;
    if (!m_stagedBinary.empty()) {
        fragSrc.assign(reinterpret_cast<const char*>(m_stagedBinary.data()), m_stagedBinary.size());
    } else {
        const std::string fragPath = MapGshPathToFragPath(shaderAssetPath);
        std::vector<uint8_t> buf;
        if (!fragPath.empty() && AssetManager::LoadBinaryFile(fragPath, buf) && !buf.empty()) {
            fragSrc.assign(reinterpret_cast<const char*>(buf.data()), buf.size());
        }
    }
    std::vector<uint8_t>{}.swap(m_stagedBinary);

    if (fragSrc.empty()) {
        fragSrc = kFallbackBlitFS;
    }

    const GLuint vs = CompileGLShader(GL_VERTEX_SHADER_VAL, kFullscreenVS);
    GLuint fs = CompileGLShader(GL_FRAGMENT_SHADER_VAL, fragSrc.c_str());
    if (fs == 0u && fragSrc != kFallbackBlitFS) {
        fs = CompileGLShader(GL_FRAGMENT_SHADER_VAL, kFallbackBlitFS);
    }
    if (vs == 0u || fs == 0u) {
        if (vs != 0u) gl.DeleteShader(vs);
        if (fs != 0u) gl.DeleteShader(fs);
        return false;
    }

    const GLuint prog = gl.CreateProgram();
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

    GLint linked = 0;
    gl.GetProgramiv(prog, GL_LINK_STATUS_VAL, &linked);
    if (linked == 0) {
        gl.DeleteProgram(prog);
        return false;
    }

    GLuint vao = 0u;
    gl.GenVertexArrays(1, &vao);
    if (vao == 0u) {
        gl.DeleteProgram(prog);
        return false;
    }

    m_program = prog;
    m_vao = vao;
    m_shaderLoaded = true;
    return true;
}

bool RHIFullscreenShader::EnableOffscreenScaler(uint32_t maxWidth, uint32_t maxHeight) {
    if (!m_shaderLoaded || maxWidth == 0u || maxHeight == 0u) return false;
    if (m_offscreenReady && m_offscreenWidth == maxWidth && m_offscreenHeight == maxHeight) {
        return true;
    }
    if (!IsLoaded()) return false;

    const auto& gl = GetFunctions();
    if (m_scalerFbo != 0u && gl.DeleteFramebuffers) {
        const GLuint id = m_scalerFbo;
        gl.DeleteFramebuffers(1, &id);
        m_scalerFbo = 0u;
    }
    if (m_scalerTex != 0u && gl.DeleteTextures) {
        const GLuint id = m_scalerTex;
        gl.DeleteTextures(1, &id);
        m_scalerTex = 0u;
    }
    m_offscreenReady = false;

    if (!gl.GenFramebuffers || !gl.BindFramebuffer || !gl.GenTextures || !gl.BindTexture ||
        !gl.TexImage2D || !gl.FramebufferTexture2D || !gl.CheckFramebufferStatus) {
        return false;
    }

    GLuint fbo = 0u;
    GLuint tex = 0u;
    gl.GenFramebuffers(1, &fbo);
    gl.GenTextures(1, &tex);
    if (fbo == 0u || tex == 0u) {
        if (tex != 0u && gl.DeleteTextures) gl.DeleteTextures(1, &tex);
        if (fbo != 0u && gl.DeleteFramebuffers) gl.DeleteFramebuffers(1, &fbo);
        return false;
    }

    if (gl.ActiveTexture) gl.ActiveTexture(GL_TEXTURE0_VAL);
    gl.BindTexture(GL_TEXTURE_2D_VAL, tex);
    gl.TexImage2D(GL_TEXTURE_2D_VAL, 0, static_cast<GLint>(GL_RGBA8_VAL),
                  static_cast<GLsizei>(maxWidth), static_cast<GLsizei>(maxHeight),
                  0, GL_RGBA_VAL, GL_UNSIGNED_BYTE_VAL, nullptr);
    gl.TexParameteri(GL_TEXTURE_2D_VAL, GL_TEXTURE_MIN_FILTER_VAL, static_cast<GLint>(GL_LINEAR_VAL));
    gl.TexParameteri(GL_TEXTURE_2D_VAL, GL_TEXTURE_MAG_FILTER_VAL, static_cast<GLint>(GL_LINEAR_VAL));
    gl.TexParameteri(GL_TEXTURE_2D_VAL, GL_TEXTURE_WRAP_S_VAL, static_cast<GLint>(GL_CLAMP_TO_EDGE_VAL));
    gl.TexParameteri(GL_TEXTURE_2D_VAL, GL_TEXTURE_WRAP_T_VAL, static_cast<GLint>(GL_CLAMP_TO_EDGE_VAL));
    gl.BindTexture(GL_TEXTURE_2D_VAL, 0u);

    gl.BindFramebuffer(GL_FRAMEBUFFER_VAL, fbo);
    gl.FramebufferTexture2D(GL_FRAMEBUFFER_VAL, GL_COLOR_ATTACHMENT0_VAL, GL_TEXTURE_2D_VAL, tex, 0);
    const GLenum status = gl.CheckFramebufferStatus(GL_FRAMEBUFFER_VAL);
    gl.BindFramebuffer(GL_FRAMEBUFFER_VAL, 0u);

    if (status != GL_FRAMEBUFFER_COMPLETE_VAL) {
        if (gl.DeleteTextures) gl.DeleteTextures(1, &tex);
        if (gl.DeleteFramebuffers) gl.DeleteFramebuffers(1, &fbo);
        return false;
    }

    m_scalerFbo = fbo;
    m_scalerTex = tex;
    m_offscreenWidth = maxWidth;
    m_offscreenHeight = maxHeight;
    m_offscreenReady = true;
    return true;
}

void RHIFullscreenShader::Shutdown() {
    if (IsLoaded()) {
        const auto& gl = GetFunctions();
        if (m_scalerFbo != 0u && gl.DeleteFramebuffers) {
            const GLuint id = m_scalerFbo;
            gl.DeleteFramebuffers(1, &id);
        }
        if (m_scalerTex != 0u && gl.DeleteTextures) {
            const GLuint id = m_scalerTex;
            gl.DeleteTextures(1, &id);
        }
        if (m_vao != 0u && gl.DeleteVertexArrays) {
            const GLuint id = m_vao;
            gl.DeleteVertexArrays(1, &id);
        }
        if (m_program != 0u && gl.DeleteProgram) {
            const GLuint id = m_program;
            gl.DeleteProgram(id);
        }
    }
    m_scalerFbo = 0u;
    m_scalerTex = 0u;
    m_vao = 0u;
    m_program = 0u;
    m_offscreenWidth = 0u;
    m_offscreenHeight = 0u;
    m_offscreenReady = false;
    m_renderingOffscreen = false;
    m_hasSavedViewport = false;
    m_shaderLoaded = false;
    std::vector<uint8_t>{}.swap(m_stagedBinary);
}

bool RHIFullscreenShader::IsReady() const noexcept {
    return m_shaderLoaded && (m_program != 0u);
}

int32_t RHIFullscreenShader::GetPixelUniformOffset(const char* uniformName) const noexcept {
    if (!IsReady() || !uniformName || !IsLoaded()) return -1;
    const auto& gl = GetFunctions();
    if (!gl.GetUniformLocation) return -1;
    return static_cast<int32_t>(gl.GetUniformLocation(m_program, uniformName));
}

void RHIFullscreenShader::BindPipeline(uint32_t viewportW, uint32_t viewportH) noexcept {
    BindPipeline(viewportW, viewportH, nullptr);
}

void RHIFullscreenShader::BindPipeline(uint32_t viewportW,
                                       uint32_t viewportH,
                                       const OutputTargetBinding* outputTarget) noexcept {
    (void)outputTarget;
    if (!IsReady() || !IsLoaded()) return;

    const auto& gl = GetFunctions();
    if (gl.GetIntegerv) {
        gl.GetIntegerv(GL_VIEWPORT_VAL, m_savedViewport);
        m_hasSavedViewport = true;
    }

    if (m_offscreenReady && m_scalerFbo != 0u) {
        m_activeViewportW = std::clamp(viewportW, 64u, m_offscreenWidth);
        m_activeViewportH = std::clamp(viewportH, 64u, m_offscreenHeight);
        m_renderingOffscreen = true;
        gl.BindFramebuffer(GL_FRAMEBUFFER_VAL, m_scalerFbo);
        gl.Disable(GL_SCISSOR_TEST_VAL);
        gl.Viewport(0, 0, static_cast<GLsizei>(m_activeViewportW), static_cast<GLsizei>(m_activeViewportH));
    } else {
        m_activeViewportW = viewportW;
        m_activeViewportH = viewportH;
        m_renderingOffscreen = false;
        gl.BindFramebuffer(GL_FRAMEBUFFER_VAL, 0u);
    }

    gl.UseProgram(m_program);
}

void RHIFullscreenShader::SetPixelUniform4fv(int32_t offset,
                                             uint32_t floatCount,
                                             const float* data) const noexcept {
    if (offset < 0 || !data || floatCount < 4u || !IsReady() || !IsLoaded()) return;
    const auto& gl = GetFunctions();
    if (gl.Uniform4fv) {
        gl.Uniform4fv(static_cast<GLint>(offset), static_cast<GLsizei>(floatCount / 4u), data);
    }
}

void RHIFullscreenShader::DrawQuad() noexcept {
    if (!IsReady() || !IsLoaded() || m_vao == 0u) return;

    const auto& gl = GetFunctions();
    gl.Disable(GL_DEPTH_TEST_VAL);
    gl.DepthMask(GL_FALSE_VAL);
    gl.Disable(GL_CULL_FACE_VAL);
    gl.Disable(GL_BLEND_VAL);
    gl.ColorMask(GL_TRUE_VAL, GL_TRUE_VAL, GL_TRUE_VAL, GL_TRUE_VAL);

    gl.BindVertexArray(m_vao);
    gl.DrawArrays(GL_TRIANGLES_VAL, 0, 3);

    if (m_renderingOffscreen && gl.BlitFramebuffer) {
        const GLint dstX0 = m_hasSavedViewport ? m_savedViewport[0] : 0;
        const GLint dstY0 = m_hasSavedViewport ? m_savedViewport[1] : 0;
        const GLint dstW  = (m_hasSavedViewport && m_savedViewport[2] > 0) ? m_savedViewport[2] : 1280;
        const GLint dstH  = (m_hasSavedViewport && m_savedViewport[3] > 0) ? m_savedViewport[3] : 720;

        gl.BindFramebuffer(GL_READ_FRAMEBUFFER_VAL, m_scalerFbo);
        gl.BindFramebuffer(GL_DRAW_FRAMEBUFFER_VAL, 0u);
        gl.BlitFramebuffer(0, 0,
                           static_cast<GLint>(m_activeViewportW),
                           static_cast<GLint>(m_activeViewportH),
                           dstX0, dstY0,
                           dstX0 + dstW, dstY0 + dstH,
                           GL_COLOR_BUFFER_BIT_VAL,
                           GL_LINEAR_VAL);
        gl.BindFramebuffer(GL_FRAMEBUFFER_VAL, 0u);
        gl.Viewport(dstX0, dstY0, dstW, dstH);
        m_renderingOffscreen = false;
    }

    gl.BindVertexArray(0u);
    gl.UseProgram(0u);
    gl.DepthMask(GL_TRUE_VAL);
}

} // namespace Neleo

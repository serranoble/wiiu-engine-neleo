#include "Graphics/RHI/RenderTargetGX2.hpp"
#include "Graphics/GL/GLFunctions.hpp"
#include <algorithm>

namespace WiiU::Graphics {

bool RenderTargetGX2::Initialize(uint32_t width,
                                 uint32_t height,
                                 const char* debugLabel,
                                 RenderTargetFormat format,
                                 bool withDepth) {
    (void)debugLabel;
    Shutdown();

    if (width == 0u || height == 0u || width > 2048u || height > 2048u) {
        return false;
    }

    if (!GL::IsLoaded() && !GL::LoadFunctions()) {
        return false;
    }

    const auto& gl = GL::GetFunctions();
    if (!gl.GenFramebuffers || !gl.BindFramebuffer || !gl.GenTextures || !gl.BindTexture ||
        !gl.TexImage2D || !gl.FramebufferTexture2D || !gl.CheckFramebufferStatus) {
        return false;
    }

    m_width = width;
    m_height = height;
    m_format = format;
    m_withDepth = withDepth;

    GL::GLuint fbo = 0u;
    GL::GLuint tex = 0u;
    gl.GenFramebuffers(1, &fbo);
    gl.GenTextures(1, &tex);
    if (fbo == 0u || tex == 0u) {
        if (tex != 0u && gl.DeleteTextures) gl.DeleteTextures(1, &tex);
        if (fbo != 0u && gl.DeleteFramebuffers) gl.DeleteFramebuffers(1, &fbo);
        return false;
    }

    if (gl.ActiveTexture) gl.ActiveTexture(GL::GL_TEXTURE0_VAL);
    gl.BindTexture(GL::GL_TEXTURE_2D_VAL, tex);
    const GL::GLint internalFmt = static_cast<GL::GLint>(
        (format == RenderTargetFormat::RGBA16_Float) ? GL::GL_RGBA16F_VAL : GL::GL_RGBA8_VAL);
    const GL::GLenum pixelType =
        (format == RenderTargetFormat::RGBA16_Float) ? GL::GL_HALF_FLOAT_VAL : GL::GL_UNSIGNED_BYTE_VAL;
    gl.TexImage2D(GL::GL_TEXTURE_2D_VAL,
                  0,
                  internalFmt,
                  static_cast<GL::GLsizei>(width),
                  static_cast<GL::GLsizei>(height),
                  0,
                  GL::GL_RGBA_VAL,
                  pixelType,
                  nullptr);
    gl.TexParameteri(GL::GL_TEXTURE_2D_VAL, GL::GL_TEXTURE_MIN_FILTER_VAL, static_cast<GL::GLint>(GL::GL_LINEAR_VAL));
    gl.TexParameteri(GL::GL_TEXTURE_2D_VAL, GL::GL_TEXTURE_MAG_FILTER_VAL, static_cast<GL::GLint>(GL::GL_LINEAR_VAL));
    gl.TexParameteri(GL::GL_TEXTURE_2D_VAL, GL::GL_TEXTURE_WRAP_S_VAL, static_cast<GL::GLint>(GL::GL_CLAMP_TO_EDGE_VAL));
    gl.TexParameteri(GL::GL_TEXTURE_2D_VAL, GL::GL_TEXTURE_WRAP_T_VAL, static_cast<GL::GLint>(GL::GL_CLAMP_TO_EDGE_VAL));
    gl.TexParameteri(GL::GL_TEXTURE_2D_VAL, GL::GL_TEXTURE_MAX_LEVEL_VAL, 0);
    gl.BindTexture(GL::GL_TEXTURE_2D_VAL, 0u);

    gl.BindFramebuffer(GL::GL_FRAMEBUFFER_VAL, fbo);
    gl.FramebufferTexture2D(GL::GL_FRAMEBUFFER_VAL, GL::GL_COLOR_ATTACHMENT0_VAL, GL::GL_TEXTURE_2D_VAL, tex, 0);

    GL::GLuint depthRbo = 0u;
    if (withDepth) {
        if (!gl.GenRenderbuffers || !gl.BindRenderbuffer || !gl.RenderbufferStorage || !gl.FramebufferRenderbuffer) {
            gl.BindFramebuffer(GL::GL_FRAMEBUFFER_VAL, 0u);
            if (gl.DeleteTextures) gl.DeleteTextures(1, &tex);
            if (gl.DeleteFramebuffers) gl.DeleteFramebuffers(1, &fbo);
            return false;
        }
        gl.GenRenderbuffers(1, &depthRbo);
        gl.BindRenderbuffer(GL::GL_RENDERBUFFER_VAL, depthRbo);
        gl.RenderbufferStorage(GL::GL_RENDERBUFFER_VAL, GL::GL_DEPTH_COMPONENT24_VAL,
                               static_cast<GL::GLsizei>(width), static_cast<GL::GLsizei>(height));
        gl.FramebufferRenderbuffer(GL::GL_FRAMEBUFFER_VAL, GL::GL_DEPTH_ATTACHMENT_VAL,
                                   GL::GL_RENDERBUFFER_VAL, depthRbo);
        gl.BindRenderbuffer(GL::GL_RENDERBUFFER_VAL, 0u);
    }

    const GL::GLenum status = gl.CheckFramebufferStatus(GL::GL_FRAMEBUFFER_VAL);
    gl.BindFramebuffer(GL::GL_FRAMEBUFFER_VAL, 0u);

    if (status != GL::GL_FRAMEBUFFER_COMPLETE_VAL) {
        if (depthRbo != 0u && gl.DeleteRenderbuffers) gl.DeleteRenderbuffers(1, &depthRbo);
        if (gl.DeleteTextures) gl.DeleteTextures(1, &tex);
        if (gl.DeleteFramebuffers) gl.DeleteFramebuffers(1, &fbo);
        return false;
    }

    m_fbo = fbo;
    m_depthRbo = depthRbo;
    m_colorTexture.glTextureId = tex;
    m_colorTexture.width = width;
    m_colorTexture.height = height;
    m_colorTexture.mipLevels = 1u;
    m_colorTexture.isCubeAtlas = false;
    m_initialized = true;
    return true;
}

void RenderTargetGX2::Shutdown() {
    if (GL::IsLoaded()) {
        const auto& gl = GL::GetFunctions();
        if (m_fbo != 0u && gl.DeleteFramebuffers) {
            const GL::GLuint id = m_fbo;
            gl.DeleteFramebuffers(1, &id);
        }
        if (m_depthRbo != 0u && gl.DeleteRenderbuffers) {
            const GL::GLuint id = m_depthRbo;
            gl.DeleteRenderbuffers(1, &id);
        }
        if (m_colorTexture.glTextureId != 0u && gl.DeleteTextures) {
            const GL::GLuint id = m_colorTexture.glTextureId;
            gl.DeleteTextures(1, &id);
        }
    }
    m_fbo = 0u;
    m_depthRbo = 0u;
    m_colorTexture = GX2Texture{};
    m_width = 0u;
    m_height = 0u;
    m_hasSavedViewport = false;
    m_initialized = false;
}

bool RenderTargetGX2::BeginPass(float clearR,
                                float clearG,
                                float clearB,
                                float clearA,
                                float clearDepth) {
    if (!m_initialized || !GL::IsLoaded() || m_fbo == 0u) return false;
    const auto& gl = GL::GetFunctions();

    if (gl.GetIntegerv) {
        gl.GetIntegerv(GL::GL_VIEWPORT_VAL, m_savedViewport);
        m_hasSavedViewport = true;
    }

    gl.BindFramebuffer(GL::GL_FRAMEBUFFER_VAL, m_fbo);
    gl.Disable(GL::GL_SCISSOR_TEST_VAL);
    gl.Viewport(0, 0, static_cast<GL::GLsizei>(m_width), static_cast<GL::GLsizei>(m_height));
    gl.ColorMask(GL::GL_TRUE_VAL, GL::GL_TRUE_VAL, GL::GL_TRUE_VAL, GL::GL_TRUE_VAL);
    gl.ClearColor(clearR, clearG, clearB, clearA);

    GL::GLbitfield mask = GL::GL_COLOR_BUFFER_BIT_VAL;
    if (m_withDepth) {
        gl.DepthMask(GL::GL_TRUE_VAL);
        mask |= GL::GL_DEPTH_BUFFER_BIT_VAL;
        (void)clearDepth;
    }
    gl.Clear(mask);
    return true;
}

void RenderTargetGX2::FlushForSampling() {
    if (!GL::IsLoaded()) return;
    const auto& gl = GL::GetFunctions();
    gl.BindFramebuffer(GL::GL_FRAMEBUFFER_VAL, 0u);
}

void RenderTargetGX2::EndPass(void* restoreContext,
                              void* restoreColor,
                              void* restoreDepth,
                              uint32_t restoreWidth,
                              uint32_t restoreHeight) {
    (void)restoreContext;
    (void)restoreColor;
    (void)restoreDepth;
    if (!GL::IsLoaded()) return;

    const auto& gl = GL::GetFunctions();
    gl.BindFramebuffer(GL::GL_FRAMEBUFFER_VAL, 0u);
    if (m_hasSavedViewport && m_savedViewport[2] > 0 && m_savedViewport[3] > 0) {
        gl.Viewport(m_savedViewport[0], m_savedViewport[1], m_savedViewport[2], m_savedViewport[3]);
    } else {
        gl.Viewport(0, 0,
                    static_cast<GL::GLsizei>(std::max(1u, restoreWidth)),
                    static_cast<GL::GLsizei>(std::max(1u, restoreHeight)));
    }
}

} // namespace WiiU::Graphics

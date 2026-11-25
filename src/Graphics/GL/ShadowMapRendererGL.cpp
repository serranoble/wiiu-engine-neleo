#include "Graphics/ShadowMapRendererGX2.hpp"
#include "Graphics/GL/GLFunctions.hpp"
#include "Graphics/Mesh/MeshBinaryFormat.hpp"
#include <algorithm>
#include <cstddef>

namespace WiiU {

namespace {

using namespace Graphics::GL;

constexpr const char* kShadowVS = R"(
#version 330 core
layout(location = 0) in vec3 aPos;
uniform mat4 uMVP;
void main() {
    gl_Position = uMVP * vec4(aPos, 1.0);
}
)";

constexpr const char* kShadowFS = R"(
#version 330 core
void main() {
}
)";

GLuint CompileShadowShader(GLenum type, const char* src) noexcept {
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

ShadowMapRendererGX2& ShadowMapRendererGX2::Instance() noexcept {
    static ShadowMapRendererGX2 s_instance;
    return s_instance;
}

bool ShadowMapRendererGX2::InitDepthShader() {
    if (m_depthProgram != 0u) return true;
    const auto& gl = GetFunctions();
    const GLuint vs = CompileShadowShader(GL_VERTEX_SHADER_VAL, kShadowVS);
    const GLuint fs = CompileShadowShader(GL_FRAGMENT_SHADER_VAL, kShadowFS);
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
    m_depthProgram = prog;
    m_mvpLoc = gl.GetUniformLocation(prog, "uMVP");
    return true;
}

bool ShadowMapRendererGX2::Initialize(uint32_t resolution) {
    if (m_initialized && m_resolution == resolution) {
        return true;
    }
    Shutdown();

    if (resolution < 256u || resolution > 2048u) {
        return false;
    }
    if (!IsLoaded() && !LoadFunctions()) {
        return false;
    }

    const auto& gl = GetFunctions();
    if (!gl.GenFramebuffers || !gl.BindFramebuffer || !gl.GenTextures || !gl.BindTexture ||
        !gl.TexImage2D || !gl.FramebufferTexture2D || !gl.CheckFramebufferStatus ||
        !gl.GenVertexArrays || !gl.DrawBuffer || !gl.ReadBuffer) {
        return false;
    }

    if (!InitDepthShader()) {
        Shutdown();
        return false;
    }

    m_resolution = resolution;

    GLuint fbo = 0u;
    GLuint tex = 0u;
    GLuint vao = 0u;
    gl.GenFramebuffers(1, &fbo);
    gl.GenTextures(1, &tex);
    gl.GenVertexArrays(1, &vao);
    if (fbo == 0u || tex == 0u || vao == 0u) {
        if (vao != 0u && gl.DeleteVertexArrays) gl.DeleteVertexArrays(1, &vao);
        if (tex != 0u && gl.DeleteTextures) gl.DeleteTextures(1, &tex);
        if (fbo != 0u && gl.DeleteFramebuffers) gl.DeleteFramebuffers(1, &fbo);
        Shutdown();
        return false;
    }

    if (gl.ActiveTexture) gl.ActiveTexture(GL_TEXTURE0_VAL);
    gl.BindTexture(GL_TEXTURE_2D_VAL, tex);
    gl.TexImage2D(GL_TEXTURE_2D_VAL,
                  0,
                  static_cast<GLint>(GL_DEPTH_COMPONENT32F_VAL),
                  static_cast<GLsizei>(resolution),
                  static_cast<GLsizei>(resolution),
                  0,
                  GL_DEPTH_COMPONENT_VAL,
                  GL_FLOAT_VAL,
                  nullptr);
    gl.TexParameteri(GL_TEXTURE_2D_VAL, GL_TEXTURE_MIN_FILTER_VAL, static_cast<GLint>(GL_NEAREST_VAL));
    gl.TexParameteri(GL_TEXTURE_2D_VAL, GL_TEXTURE_MAG_FILTER_VAL, static_cast<GLint>(GL_NEAREST_VAL));
    gl.TexParameteri(GL_TEXTURE_2D_VAL, GL_TEXTURE_WRAP_S_VAL, static_cast<GLint>(GL_CLAMP_TO_BORDER_VAL));
    gl.TexParameteri(GL_TEXTURE_2D_VAL, GL_TEXTURE_WRAP_T_VAL, static_cast<GLint>(GL_CLAMP_TO_BORDER_VAL));
    gl.TexParameteri(GL_TEXTURE_2D_VAL, GL_TEXTURE_MAX_LEVEL_VAL, 0);
    if (gl.TexParameterfv) {
        static constexpr GLfloat kBorder[4] = {1.0f, 1.0f, 1.0f, 1.0f};
        gl.TexParameterfv(GL_TEXTURE_2D_VAL, GL_TEXTURE_BORDER_COLOR_VAL, kBorder);
    }
    gl.BindTexture(GL_TEXTURE_2D_VAL, 0u);

    gl.BindFramebuffer(GL_FRAMEBUFFER_VAL, fbo);
    gl.FramebufferTexture2D(GL_FRAMEBUFFER_VAL, GL_DEPTH_ATTACHMENT_VAL, GL_TEXTURE_2D_VAL, tex, 0);
    gl.DrawBuffer(GL_NONE_VAL);
    gl.ReadBuffer(GL_NONE_VAL);
    const GLenum status = gl.CheckFramebufferStatus(GL_FRAMEBUFFER_VAL);
    gl.BindFramebuffer(GL_FRAMEBUFFER_VAL, 0u);

    if (status != GL_FRAMEBUFFER_COMPLETE_VAL) {
        gl.DeleteVertexArrays(1, &vao);
        gl.DeleteTextures(1, &tex);
        gl.DeleteFramebuffers(1, &fbo);
        Shutdown();
        return false;
    }

    m_shadowFbo = fbo;
    m_shadowVao = vao;
    m_shadowTexture.glTextureId = tex;
    m_shadowTexture.width = resolution;
    m_shadowTexture.height = resolution;
    m_shadowTexture.mipLevels = 1u;
    m_shadowTexture.isCubeAtlas = false;
    m_inShadowPass = false;
    m_initialized = true;
    return true;
}

void ShadowMapRendererGX2::Shutdown() {
    if (IsLoaded()) {
        const auto& gl = GetFunctions();
        if (m_shadowFbo != 0u && gl.DeleteFramebuffers) {
            const GLuint id = m_shadowFbo;
            gl.DeleteFramebuffers(1, &id);
        }
        if (m_shadowVao != 0u && gl.DeleteVertexArrays) {
            const GLuint id = m_shadowVao;
            gl.DeleteVertexArrays(1, &id);
        }
        if (m_shadowTexture.glTextureId != 0u && gl.DeleteTextures) {
            const GLuint id = m_shadowTexture.glTextureId;
            gl.DeleteTextures(1, &id);
        }
        if (m_depthProgram != 0u && gl.DeleteProgram) {
            gl.DeleteProgram(m_depthProgram);
        }
    }
    m_shadowFbo = 0u;
    m_shadowVao = 0u;
    m_depthProgram = 0u;
    m_mvpLoc = -1;
    m_shadowTexture = GX2Texture{};
    m_hasSavedViewport = false;
    m_inShadowPass = false;
    m_initialized = false;
}

bool ShadowMapRendererGX2::BeginShadowPass(const Matrix4x4& lightViewProj,
                                           void* contextState,
                                           bool cullFrontFaces,
                                           float depthBiasUnits,
                                           float depthBiasSlope) {
    (void)contextState;
    if (!m_initialized || !IsLoaded() || m_shadowFbo == 0u || m_depthProgram == 0u) return false;

    const auto& gl = GetFunctions();
    m_lightViewProj = lightViewProj;
    m_inShadowPass = true;

    if (gl.GetIntegerv) {
        gl.GetIntegerv(GL_VIEWPORT_VAL, m_savedViewport);
        m_hasSavedViewport = true;
    }

    gl.BindFramebuffer(GL_FRAMEBUFFER_VAL, m_shadowFbo);
    gl.Disable(GL_SCISSOR_TEST_VAL);
    gl.Viewport(0, 0, static_cast<GLsizei>(m_resolution), static_cast<GLsizei>(m_resolution));
    gl.Enable(GL_DEPTH_TEST_VAL);
    gl.DepthMask(GL_TRUE_VAL);
    gl.DepthFunc(GL_LESS_VAL);
    gl.ColorMask(GL_FALSE_VAL, GL_FALSE_VAL, GL_FALSE_VAL, GL_FALSE_VAL);
    gl.Clear(GL_DEPTH_BUFFER_BIT_VAL);

    if (cullFrontFaces) {
        gl.Enable(GL_CULL_FACE_VAL);
        gl.CullFace(GL_FRONT_VAL);
        gl.FrontFace(GL_CCW_VAL);
    } else {
        gl.Disable(GL_CULL_FACE_VAL);
    }

    if ((depthBiasSlope != 0.0f || depthBiasUnits != 0.0f) && gl.PolygonOffset) {
        gl.Enable(GL_POLYGON_OFFSET_FILL_VAL);
        gl.PolygonOffset(depthBiasSlope, depthBiasUnits);
    } else {
        gl.Disable(GL_POLYGON_OFFSET_FILL_VAL);
    }

    gl.UseProgram(m_depthProgram);
    return true;
}

void ShadowMapRendererGX2::RenderCaster(const StaticMesh& mesh, const Matrix4x4& modelMatrix) {
    if (!mesh.IsLoaded() || !mesh.GetVBO() || !mesh.GetIBO()) return;
    RenderCasterBuffers(*const_cast<GX2RBuffer*>(mesh.GetVBO()),
                        *const_cast<GX2RBuffer*>(mesh.GetIBO()),
                        mesh.GetIndexCount(),
                        mesh.GetHeader().vertexStride,
                        modelMatrix,
                        mesh.GetHeader().indexFormat == 1u);
}

void ShadowMapRendererGX2::RenderCasterBuffers(GX2RBuffer& vbo,
                                               GX2RBuffer& ibo,
                                               uint32_t indexCount,
                                               uint32_t vertexStride,
                                               const Matrix4x4& modelMatrix,
                                               bool use32BitIndices) {
    if (!m_inShadowPass || !IsLoaded() || vbo.glBufferId == 0u || ibo.glBufferId == 0u || indexCount == 0u) {
        return;
    }

    const auto& gl = GetFunctions();
    const Matrix4x4 mvp = m_lightViewProj * modelMatrix;
    gl.UniformMatrix4fv(m_mvpLoc, 1, GL_FALSE_VAL, mvp.m);

    gl.BindVertexArray(m_shadowVao);
    gl.BindBuffer(GL_ARRAY_BUFFER_VAL, vbo.glBufferId);
    gl.EnableVertexAttribArray(0u);
    gl.VertexAttribPointer(0u, 3, GL_FLOAT_VAL, GL_FALSE_VAL, static_cast<GLsizei>(vertexStride),
                           reinterpret_cast<const void*>(offsetof(MeshVertex, pos)));
    gl.BindBuffer(GL_ELEMENT_ARRAY_BUFFER_VAL, ibo.glBufferId);
    gl.DrawElements(GL_TRIANGLES_VAL,
                    static_cast<GLsizei>(indexCount),
                    use32BitIndices ? GL_UNSIGNED_INT_VAL : GL_UNSIGNED_SHORT_VAL,
                    nullptr);
}

void ShadowMapRendererGX2::EndShadowPass(void* contextState,
                                         void* restoreColor,
                                         void* restoreDepth,
                                         uint32_t restoreWidth,
                                         uint32_t restoreHeight) {
    (void)contextState;
    (void)restoreColor;
    (void)restoreDepth;
    if (!IsLoaded()) return;

    const auto& gl = GetFunctions();
    m_inShadowPass = false;
    gl.Disable(GL_POLYGON_OFFSET_FILL_VAL);
    gl.CullFace(GL_BACK_VAL);
    gl.Disable(GL_CULL_FACE_VAL);
    gl.ColorMask(GL_TRUE_VAL, GL_TRUE_VAL, GL_TRUE_VAL, GL_TRUE_VAL);
    gl.BindVertexArray(0u);
    gl.UseProgram(0u);
    gl.BindFramebuffer(GL_FRAMEBUFFER_VAL, 0u);
    if (m_hasSavedViewport && m_savedViewport[2] > 0 && m_savedViewport[3] > 0) {
        gl.Viewport(m_savedViewport[0], m_savedViewport[1], m_savedViewport[2], m_savedViewport[3]);
    } else {
        gl.Viewport(0, 0,
                    static_cast<GLsizei>(std::max(1u, restoreWidth)),
                    static_cast<GLsizei>(std::max(1u, restoreHeight)));
    }
}

void ShadowMapRendererGX2::BindShadowMap(uint32_t texSlot, uint32_t samplerSlot) const {
    BindShadowMapRaw(texSlot, samplerSlot);
}

void ShadowMapRendererGX2::BindShadowMapRaw(uint32_t texSlot, uint32_t samplerSlot) const {
    (void)samplerSlot;
    if (!m_initialized || !IsLoaded() || m_shadowTexture.glTextureId == 0u) return;
    const auto& gl = GetFunctions();
    gl.ActiveTexture(GL_TEXTURE0_VAL + texSlot);
    gl.BindTexture(GL_TEXTURE_2D_VAL, m_shadowTexture.glTextureId);
    if (texSlot != 0u) {
        gl.ActiveTexture(GL_TEXTURE0_VAL);
    }
}

} // namespace WiiU

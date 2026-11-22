#pragma once

#include "Graphics/Math3D.hpp"
#include "Graphics/MaterialSystem.hpp"
#include "Graphics/RHI/RHIPipelineCache.hpp"
#include "Graphics/RHI/RHIPipelineState.hpp"
#include "Graphics/Mesh/StaticMesh.hpp"
#include "Core/Config/EngineConfig.hpp"
#include <cstdint>
#include <string_view>

#if defined(__WIIU__) || defined(__wiiu__)
#include <gx2/shaders.h>
#include <gx2/texture.h>
#include <gx2/sampler.h>
#include <gx2r/buffer.h>
#include <gx2r/draw.h>
#else
struct GX2Texture;
struct GX2RBuffer;
#endif

namespace WiiU {

/**
 * @brief ForwardRendererGX2: Subsistema de renderizado forward desacoplado para Nintendo Wii U (Latte GX2).
 * 
 * Centraliza y orquesta:
 * 1. Pipeline de Skydome / Bóveda celeste infinita (Z=W=1.0).
 * 2. Pipeline de Mallas Avanzadas con sombreado Half-Lambert, normales analíticas en pantalla y materiales.
 * 3. Pipeline de Animación Esquelética LBS por hardware (hasta 24 huesos).
 * 
 * Disciplina estricta:
 * - Cero alocaciones dinámicas a 60 FPS en BeginFrame/Render/EndFrame.
 * - Uniformes alineados a 256 bytes para la jerarquía de memoria de Latte GX2.
 * - Soporte de fallback determinista por si fallan los assets en disco.
 */
class ForwardRendererGX2 {
public:
    static ForwardRendererGX2& Instance() noexcept;

    // Inicializa todos los shaders, fetch shaders y samplers de silicio
    bool Initialize();

    // Actualiza dinámicamente el modo de filtrado de textura en hardware GX2
    void SetTextureFilterMode(TextureFilterMode mode);

    // Libera recursos asignados en MemoryManager
    void Shutdown();

    // Comienza el pase de renderizado 3D configurando cámara y matrices de vista/proyección
    void BeginFrame(const Point3D& camPos, const Matrix4x4& viewMatrix, const Matrix4x4& projMatrix);

    // Dibuja el domo hemisférico infinito en el fondo
    void RenderSkydome(StaticMesh& skydomeMesh, GX2Texture* skyTexture);

    // Dibuja una malla estática con transformación, textura difusa, parámetros de material y normal mapping
    void RenderStaticMesh(const StaticMesh& mesh, 
                         const Matrix4x4& modelMatrix, 
                         GX2Texture* diffuseTexture,
                         GX2Texture* envCubeMap = nullptr,
                         float reflectionIntensity = 0.0f,
                         bool normalMapEnabled = true,
                         float bumpScale = 2.0f,
                         float ambientIntensity = 0.38f);

    // [OPT-13 / OPT-365] Dibuja una malla estatica consumiendo directamente un MaterialPropertyBlock UBO 256B
    void RenderStaticMeshWithMaterial(const StaticMesh& mesh,
                                      const Matrix4x4& modelMatrix,
                                      const MaterialPropertyBlock& material);

    // [OPT-20 / OPT-319] Hardware Geometry Instancing (Multi-Frame Ring Buffer + stream divisor=1)
    static constexpr uint32_t MAX_FRAMES_IN_FLIGHT     = 3;
    static constexpr uint32_t MAX_INSTANCES_PER_FRAME  = 1024;
    static constexpr uint32_t MAX_INSTANCES_PER_DRAW   = 256;
    static constexpr uint32_t INSTANCE_RING_CAPACITY   = MAX_FRAMES_IN_FLIGHT * MAX_INSTANCES_PER_FRAME;
    void RenderStaticMeshInstanced(const StaticMesh& mesh,
                                   const Matrix4x4* modelMatrices,
                                   uint32_t instanceCount,
                                   GX2Texture* diffuseTexture,
                                   GX2Texture* envCubeMap = nullptr,
                                   const MaterialPropertyBlock* material = nullptr);

    // [OPT-343] Lote instanciado de mallas esqueléticas con enlace de pipeline único (zero redundant binds)
    void RenderSkinnedMeshInstanced(const StaticMesh& skinnedMesh,
                                    const Matrix4x4* modelMatrices,
                                    const Matrix4x4* const* bonePalettes,
                                    uint32_t boneCount,
                                    uint32_t instanceCount,
                                    GX2Texture* diffuseTexture,
                                    GX2Texture* envCubeMap = nullptr,
                                    float reflectionIntensity = 0.0f,
                                    bool clayMode = false);

    // [OPT-26 / OPT-134 / OPT-161 / OPT-261] Pase Hardware Early-Z Depth Pre-Pass (Color Writes OFF)
    void BeginDepthPrePass();
    void RenderDepthPrePassBuffers(GX2RBuffer& vbo,
                                   GX2RBuffer& ibo,
                                   uint32_t indexCount,
                                   uint32_t vertexStride,
                                   const Matrix4x4& modelMatrix,
                                   bool use32BitIndices = false);
    void EndDepthPrePass();

    [[nodiscard]] const Point3D& GetCameraPosition() const noexcept { return m_camPos; }
    [[nodiscard]] const Matrix4x4& GetViewMatrix() const noexcept { return m_viewMatrix; }
    [[nodiscard]] const Matrix4x4& GetViewProjMatrix() const noexcept { return m_vpMatrix; }

    // Dibuja un componente de malla ECS estático directamente desde sus buffers de hardware
    void RenderMeshBuffers(GX2RBuffer& vbo,
                          GX2RBuffer& ibo,
                          uint32_t indexCount,
                          uint32_t vertexStride,
                          const Matrix4x4& modelMatrix,
                          GX2Texture* diffuseTexture,
                          GX2Texture* envCubeMap = nullptr,
                          float reflectionIntensity = 0.0f,
                          bool normalMapEnabled = true,
                          float bumpScale = 2.0f,
                          float ambientIntensity = 0.38f,
                          bool use32BitIndices = false,
                          BlendMode blendMode = BlendMode::Opaque,
                          bool depthTest = true,
                          bool depthWrite = true,
                          uint8_t depthCompare = RHI_COMPARE_LEQUAL);

    // Dibuja una malla skinneda con LBS acelerado por hardware desde StaticMesh
    void RenderSkinnedMesh(const StaticMesh& skinnedMesh,
                          const Matrix4x4& modelMatrix,
                          const Matrix4x4* boneMatrices,
                          uint32_t boneCount,
                          GX2Texture* diffuseTexture,
                          GX2Texture* envCubeMap = nullptr,
                          float reflectionIntensity = 0.0f,
                          bool clayMode = false);

    // Dibuja una malla skinneda con LBS acelerado por hardware
    void RenderSkinnedMesh(GX2RBuffer& vbo, 
                          GX2RBuffer& ibo, 
                          uint32_t indexCount, 
                          uint32_t vertexStride,
                          const Matrix4x4& modelMatrix, 
                          const Matrix4x4* boneMatrices, 
                          uint32_t boneCount,
                          GX2Texture* diffuseTexture,
                          GX2Texture* envCubeMap = nullptr,
                          float reflectionIntensity = 0.0f,
                          bool clayMode = false,
                          bool use32BitIndices = false);

    // Concluye el frame 3D
    void EndFrame();

    // Métricas de telemetría por frame
    [[nodiscard]] uint32_t GetDrawCallsCount() const noexcept { return m_drawCallsCount; }
    [[nodiscard]] uint32_t GetTrianglesCount() const noexcept { return m_trianglesCount; }

private:
    ForwardRendererGX2() noexcept = default;
    ~ForwardRendererGX2() = default;

    ForwardRendererGX2(const ForwardRendererGX2&) = delete;
    ForwardRendererGX2& operator=(const ForwardRendererGX2&) = delete;
    ForwardRendererGX2(ForwardRendererGX2&&) = delete;
    ForwardRendererGX2& operator=(ForwardRendererGX2&&) = delete;

    bool InitSkyShader();
    bool InitMeshShader();
    bool InitSkinningShader();
    bool InitInstancedShader();
    bool InitDepthPrePassShader();
    void ApplyTextureFilterMode(TextureFilterMode mode);

    bool m_initialized{false};

    // Matrices por cuadro
    Point3D m_camPos{0.0f, 0.0f, 0.0f};
    Matrix4x4 m_viewMatrix{};
    Matrix4x4 m_projMatrix{};
    Matrix4x4 m_vpMatrix{};

    // Telemetría
    uint32_t m_drawCallsCount{0};
    uint32_t m_trianglesCount{0};

#if defined(__WIIU__) || defined(__wiiu__)
    // 1. Pipeline Skydome
    GX2VertexShader* m_skyVs{nullptr};
    void* m_skyVsProgram{nullptr};
    GX2PixelShader* m_skyPs{nullptr};
    void* m_skyPsProgram{nullptr};
    GX2FetchShader m_skyFs{};
    void* m_skyFsProgram{nullptr};
    GX2Sampler m_skySampler{};
    bool m_skyShaderLoaded{false};

    // 2. Pipeline Mallas Avanzadas
    GX2VertexShader* m_meshVs{nullptr};
    void* m_meshVsProgram{nullptr};
    GX2PixelShader* m_meshPs{nullptr};
    void* m_meshPsProgram{nullptr};
    GX2FetchShader m_meshFs{};
    void* m_meshFsProgram{nullptr};
    GX2Sampler m_meshSampler{};
    bool m_meshShaderLoaded{false};

    // 3. Pipeline Animación Esquelética LBS
    GX2VertexShader* m_skelVs{nullptr};
    void* m_skelVsProgram{nullptr};
    GX2PixelShader* m_skelPs{nullptr};
    void* m_skelPsProgram{nullptr};
    GX2FetchShader m_skelFs{};
    void* m_skelFsProgram{nullptr};
    bool m_skelShaderLoaded{false};

    // Sampler para CubeMaps ambientales
    GX2Sampler m_cubeSampler{};
    bool m_cubeSamplerCreated{false};

    // Textura neutra fallback (0,0,0,0) para activar vertex coloring en mallas sin textura difusa
    GX2Texture m_defaultNeutralTex{};
    bool m_defaultTexCreated{false};

    // CubeMap neutro fallback 1x1 (6 caras) para garantizar slot 1 valido en shaders branchless
    GX2Texture m_defaultNeutralCubeMap{};
    bool m_defaultCubeCreated{false};

    // 4. [OPT-20 / OPT-319] Pipeline Hardware Geometry Instancing (Multi-Frame Ring Buffer, divisor=1)
    GX2VertexShader* m_instVs{nullptr};
    void* m_instVsProgram{nullptr};
    GX2PixelShader* m_instPs{nullptr};
    void* m_instPsProgram{nullptr};
    GX2FetchShader m_instFs{};
    void* m_instFsProgram{nullptr};
    GX2RBuffer m_instanceMatrixBuffer{};
    uint32_t m_instanceFrameSlice{0};
    uint32_t m_instanceWriteCursor{0};
    bool m_instanceBufferCreated{false};
    bool m_instShaderLoaded{false};

    // 5. [OPT-26 / OPT-161] Pipeline Hardware Early-Z Depth Pre-Pass (Null Pixel Shader)
    GX2VertexShader* m_depthPreVs{nullptr};
    void* m_depthPreVsProgram{nullptr};
    GX2FetchShader m_depthPreFs{};
    void* m_depthPreFsProgram{nullptr};
    bool m_depthPreShaderLoaded{false};
    bool m_inDepthPrePass{false};
#endif
};

} // namespace WiiU

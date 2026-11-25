#pragma once

#include <cstddef>
#include <cstdint>
#include <string_view>

#if defined(__WIIU__) || defined(__wiiu__)
#include <gx2/texture.h>
#else
struct GX2Texture {
    uint32_t glTextureId{0u};
    uint32_t width{0u};
    uint32_t height{0u};
    uint32_t mipLevels{1u};
    bool isCubeAtlas{false};
};
#endif

namespace WiiU::Graphics::TextureLoaderGX2 {

/**
 * @brief Carga una textura 2D cocinada (.wtex: RGBA8, BC1 o BC3) desde memoria alineada en MEM2.
 *
 * Configura la superficie Latte GX2, alinea en MemoryManager(MemoryTag::Texture),
 * realiza flush DMA con GX2Invalidate(GX2_INVALIDATE_MODE_CPU_TEXTURE) e inicializa registros HW.
 */
[[nodiscard]] bool LoadCookedTextureFromMemory(const uint8_t* data,
                                               size_t size,
                                               GX2Texture& outTex,
                                               const char* debugLabel = "CookedTex") noexcept;

/**
 * @brief Carga una textura 2D cocinada (.wtex) desde archivo via AssetManager.
 */
[[nodiscard]] bool LoadCookedTextureFromFile(std::string_view filepath,
                                             GX2Texture& outTex,
                                             const char* debugLabel = "CookedTex") noexcept;

/**
 * @brief Crea una textura 2D sólida 4x4 (por defecto blanco 0xFFFFFFFF) como fallback neutral o tintado.
 */
[[nodiscard]] bool CreateSolidColorTexture(GX2Texture& outTex,
                                           uint32_t rgba8Color = 0xFFFFFFFFu,
                                           const char* debugLabel = "SolidColorTex") noexcept;

/**
 * @brief Crea una textura 2D procedural de tablero (checkerboard) RGBA8 con cadena completa de Mipmaps en GX2.
 * @param outTex Textura destino inicializada en MEM2 (image + mipmaps alineados a >= 256B).
 * @param size Dimension potencia de 2 (ej. 64, 128, 256, 512).
 * @param checkerCells Cantidad de celdas por eje en el nivel base (ej. 8 o 16).
 * @param colorCodeMips Si es true, cada nivel de mipmap usa un tinte distinto para inspeccion visual de LOD/Trilinear/Aniso.
 * @param debugLabel Etiqueta de diagnostico en MemoryManager.
 */
[[nodiscard]] bool CreateProceduralMipmapCheckerTexture(GX2Texture& outTex,
                                                        uint32_t size = 256u,
                                                        uint32_t checkerCells = 8u,
                                                        bool colorCodeMips = false,
                                                        const char* debugLabel = "MipCheckerTex");

/**
 * @brief Crea una textura 2D procedural de relieve biselado (ladrillos/mampostería) RGBA8 con cadena completa de Mipmaps.
 * Ideal para validar Normal/Bump Mapping en espacio tangente (TBN) con luz rasante.
 */
[[nodiscard]] bool CreateProceduralReliefTexture(GX2Texture& outTex,
                                                 uint32_t size = 256u,
                                                 const char* debugLabel = "ReliefBrickTex");

/**
 * @brief Carga un CubeMap ambiental de 6 caras (.wcube RGBA8) desde memoria.
 */
[[nodiscard]] bool LoadCookedCubeMapFromMemory(const uint8_t* data,
                                               size_t size,
                                               GX2Texture& outTex,
                                               const char* debugLabel = "CookedCubeMap") noexcept;

/**
 * @brief Carga un CubeMap ambiental (.wcube) desde archivo via AssetManager.
 */
[[nodiscard]] bool LoadCookedCubeMapFromFile(std::string_view filepath,
                                             GX2Texture& outTex,
                                             const char* debugLabel = "CookedCubeMap") noexcept;

/**
 * @brief Crea un CubeMap procedural de 6 caras (cielo azul, disco solar, horizonte cálido y mar/suelo)
 *        en memoria alineada GX2 para Skybox, IBL y pruebas autocontenidas (Suite G4 / Self-Test T13).
 */
[[nodiscard]] bool CreateProceduralSkyCubeMap(GX2Texture& outTex,
                                              uint32_t faceSize = 128u,
                                              const char* debugLabel = "ProcSkyCube");

/**
 * @brief Crea un Atlas 2D 3x2 de 6 caras (+X,-X,+Y / -Y,+Z,-Z) en GX2_SURFACE_DIM_TEXTURE_2D
 *        para muestreo analítico SampleCubeAtlas() sin instrucción hardware R600 CUBE.
 */
[[nodiscard]] bool CreateProceduralSkyCubeAtlas(GX2Texture& outTex,
                                                uint32_t faceSize = 128u,
                                                const char* debugLabel = "ProcSkyAtlas");

/**
 * @brief Carga un .wcube de 6 caras desde memoria y lo empaqueta como Atlas 2D 3x2 (TEXTURE_2D).
 */
[[nodiscard]] bool LoadCookedCubeMapAsAtlasFromMemory(const uint8_t* data,
                                                      size_t size,
                                                      GX2Texture& outTex,
                                                      const char* debugLabel = "CookedCubeAtlas") noexcept;

/**
 * @brief Carga un .wcube de 6 caras desde archivo via AssetManager y lo empaqueta como Atlas 2D 3x2.
 */
[[nodiscard]] bool LoadCookedCubeMapAsAtlasFromFile(std::string_view filepath,
                                                    GX2Texture& outTex,
                                                    const char* debugLabel = "CookedCubeAtlas") noexcept;

/**
 * @brief Crea una textura 2D procedural de celosía / follaje con canal Alpha binario (0/255)
 *        para validar Alpha-Cutout (discard) en GX2 (Suite G5 / Self-Test T17).
 */
[[nodiscard]] bool CreateProceduralAlphaCutoutTexture(GX2Texture& outTex,
                                                      uint32_t size = 128u,
                                                      const char* debugLabel = "AlphaCutoutTex");

/**
 * @brief Libera de forma segura los búferes image/mipmaps de una GX2Texture asignada en MemoryManager.
 * @note El llamador debe asegurar que la GPU terminó su uso (p. ej. tras GX2DrawDone()).
 */
void DestroyTexture(GX2Texture& tex) noexcept;

} // namespace WiiU::Graphics::TextureLoaderGX2

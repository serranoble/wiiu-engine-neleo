#pragma once

#include <cstddef>
#include <cstdint>
#include <string_view>

#if defined(__WIIU__) || defined(__wiiu__)
#include <gx2/texture.h>
#else
struct GX2Texture;
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
 * @brief Libera de forma segura los búferes image/mipmaps de una GX2Texture asignada en MemoryManager.
 * @note El llamador debe asegurar que la GPU terminó su uso (p. ej. tras GX2DrawDone()).
 */
void DestroyTexture(GX2Texture& tex) noexcept;

} // namespace WiiU::Graphics::TextureLoaderGX2

#pragma once

#include <cstdint>

namespace Neleo {

/**
 * @brief [OPT-243] Componente de Malla y Geometría 3D orientado a datos.
 * 
 * Describe los búferes de GPU (VBO / IBO), recuentos de índices y primitivas para
 * renderizado directo en silicio AMD Latte GX2 o software renderer.
 */
struct MeshComponent {
    void* vbo{nullptr};
    void* ibo{nullptr};
    uint32_t indexCount{0};
    uint32_t vertexCount{0};
    uint32_t primitiveMode{4}; // GX2_PRIMITIVE_MODE_TRIANGLES
    uint16_t vertexStride{32}; // Bytes por vértice
    uint8_t indexType{0};      // 0 = U16, 1 = U32
    bool isVisible{true};
    bool castShadows{true};

    constexpr MeshComponent() noexcept = default;

    constexpr MeshComponent(void* pVbo, void* pIbo, uint32_t indices, uint32_t vertices = 0,
                            uint32_t primMode = 4, uint16_t stride = 32, uint8_t idxType = 0) noexcept
        : vbo(pVbo), ibo(pIbo), indexCount(indices), vertexCount(vertices),
          primitiveMode(primMode), vertexStride(stride), indexType(idxType),
          isVisible(true), castShadows(true) {}
};

} // namespace Neleo

#pragma once

#include <cstdint>
#include <cstddef>

namespace Neleo {

/**
 * @brief [OPT-293] Formato Binario Universal de Mallas 3D para Nintendo Wii U (.wmesh).
 * 
 * Diseñado bajo las directrices de Jason Gregory (Game Engine Architecture, 3rd Ed., Cap. 12 & 13)
 * para carga instantánea con CERO alocaciones dinámicas en caliente y CERO coste de byte-swapping:
 * 
 * 1. Big-Endian Nativo: Los números enteros y de punto flotante se empaquetan en orden Big-Endian
 *    coincidente con la CPU IBM PowerPC 750CL (Espresso) y la GPU AMD Latte (R700).
 * 2. Alineación de Hardware: Los datos del VBO están alineados a 256 bytes (requisito de DMA de GX2)
 *    y los del IBO a 32 bytes (coincidente con la línea de caché L1D del procesador Espresso).
 * 3. Volúmenes Envolventes Precalculados: Esfera delimitadora y AABB calculados offline en el cooker,
 *    permitiendo poblar BoundingVolumeComponent en O(1) sin recorrer vértices en la consola.
 * 4. Optimización de Caché de Vértices PTVC: Triángulos reordenados offline mediante Tipsify/Forsyth
 *    para maximizar el reuso de la caché de vértices posterior a la transformación de AMD Latte.
 */

constexpr uint32_t WMESH_MAGIC = 0x574D5348; // 'WMSH' en ASCII (0x57, 0x4D, 0x53, 0x48)
constexpr uint16_t WMESH_VERSION_CURRENT = 1;

// Banderas de características de la malla
enum MeshHeaderFlags : uint16_t {
    MESH_FLAG_STATIC       = 1 << 0, // Malla estática rígida (sin skinning esquelético)
    MESH_FLAG_SKINNED      = 1 << 1, // Malla con animación esquelética (pesos y huesos)
    MESH_FLAG_HAS_COLOR    = 1 << 2, // Vértices contienen aColor (vec4 float)
    MESH_FLAG_HAS_TEXCOORD = 1 << 3, // Vértices contienen aTexCoord (vec2 float)
    MESH_FLAG_HAS_NORMAL   = 1 << 4, // Vértices contienen aNormal (vec3 float)
    MESH_FLAG_BIG_ENDIAN   = 1 << 5, // Datos serializados en formato PowerPC Big-Endian
    MESH_FLAG_TANGENTS     = 1 << 6  // Vértices contienen tangentes para normal mapping
};

/**
 * @brief Cabecera de archivo binario .wmesh (96 bytes exactos, múltiplo de línea de caché L1D de 32B).
 */
#pragma pack(push, 4)
struct MeshHeader {
    uint32_t magic;           // 0x00: 'WMSH' (0x574D5348)
    uint16_t version;         // 0x04: Versión del formato (1)
    uint16_t flags;           // 0x06: Máscara de bits MeshHeaderFlags
    uint32_t vertexCount;     // 0x08: Número total de vértices
    uint32_t indexCount;      // 0x0C: Número total de índices de primitiva
    uint32_t vertexStride;    // 0x10: Tamaño en bytes de cada vértice (48 bytes)
    uint32_t indexFormat;     // 0x14: 0 = U16 (GX2_INDEX_TYPE_U16), 1 = U32
    uint32_t vboOffset;       // 0x18: Desplazamiento en bytes al VBO (alineado a 256 bytes)
    uint32_t vboSize;         // 0x1C: Tamaño en bytes de los datos de vértices
    uint32_t iboOffset;       // 0x20: Desplazamiento en bytes al IBO (alineado a 32 bytes)
    uint32_t iboSize;         // 0x24: Tamaño en bytes de los datos de índices
    float boundingSphere[4];  // 0x28: Centro X, Y, Z y Radio de la esfera envolvente local
    float aabbMin[3];         // 0x38: Caja envolvente mínima (X, Y, Z)
    float aabbMax[3];         // 0x44: Caja envolvente máxima (X, Y, Z)
    uint32_t submeshCount;    // 0x50: Cantidad de submallas (por defecto 1)
    uint32_t reserved[3];     // 0x54: Relleno a 96 bytes (3 líneas de caché de 32 bytes)
};
#pragma pack(pop)

static_assert(sizeof(MeshHeader) == 96, "MeshHeader debe tener exactamente 96 bytes para alineacion L1D");

/**
 * @brief Vértice de Malla Estática para Silicio GX2 (48 bytes, alineado a 16 bytes).
 * Coincide 1:1 con `EnvironmentVertex` y los shaders CafeGLSL del motor.
 */
#pragma pack(push, 4)
struct MeshVertex {
    float pos[3];      // 0x00: Posición vec3 (X, Y, Z)
    float normal[3];   // 0x0C: Normal unitaria vec3 (NX, NY, NZ)
    float texCoord[2]; // 0x18: Coordenadas UV vec2 (U, V)
    float color[4];    // 0x20: Color RGBA vec4 (R, G, B, A en [0.0, 1.0])
};
#pragma pack(pop)

static_assert(sizeof(MeshVertex) == 48, "MeshVertex debe tener exactamente 48 bytes");

/**
 * @brief [OPT-331] Vértice de Malla Esquelética Articulada para Silicio Latte GX2 (80 bytes, alineado a 16 bytes).
 * Compatible 1:1 con hardware fetching de GX2:
 * - aPos: offset 0 (FLOAT 32_32_32)
 * - aNormal: offset 12 (FLOAT 32_32_32)
 * - aTexCoord: offset 24 (FLOAT 32_32)
 * - aColor: offset 32 (FLOAT 32_32_32_32)
 * - aBoneIndices: offset 48 (UINT 8_8_8_8)
 * - aBoneWeights: offset 52 (FLOAT 32_32_32_32)
 * - padding: offset 68 (12 bytes relleno a 80 bytes)
 */
#pragma pack(push, 4)
struct SkinnedMeshVertex {
    float pos[3];           // 0x00: Posición vec3 (X, Y, Z)
    float normal[3];        // 0x0C: Normal unitaria vec3 (NX, NY, NZ)
    float texCoord[2];      // 0x18: Coordenadas UV vec2 (U, V)
    float color[4];         // 0x20: Color RGBA vec4 (R, G, B, A)
    uint8_t boneIndices[4]; // 0x30: Índices uvec4 de huesos [0..63]
    float boneWeights[4];   // 0x34: Pesos normalizados vec4 (suman 1.0f)
    float padding[3];       // 0x44: Relleno a 80 bytes (múltiplo de 16B)
};
#pragma pack(pop)

static_assert(sizeof(SkinnedMeshVertex) == 80, "SkinnedMeshVertex debe tener exactamente 80 bytes");
inline constexpr uint32_t SKINNED_VERTEX_STRIDE = static_cast<uint32_t>(sizeof(SkinnedMeshVertex));

/**
 * @brief Descriptor de Submalla para materiales múltiples en un solo modelo.
 */
#pragma pack(push, 4)
struct MeshSubmesh {
    uint32_t indexStart;
    uint32_t indexCount;
    uint32_t materialId;
    float boundingSphere[4]; // Centro local y radio de la submalla (16 bytes)
    uint32_t reserved;       // Relleno a 32 bytes (línea de caché L1D)
};
#pragma pack(pop)

static_assert(sizeof(MeshSubmesh) == 32, "MeshSubmesh debe tener exactamente 32 bytes");

} // namespace Neleo

#pragma once

#include "Graphics/Mesh/MeshBinaryFormat.hpp"
#include "Graphics/Math3D.hpp"
#include "Core/ECS/Components/MeshComponent.hpp"
#include "Core/ECS/Components/BoundingVolumeComponent.hpp"
#include <string>
#include <string_view>
#include <vector>
#include <cstdint>

#if defined(__WIIU__) || defined(__wiiu__)
#include <gx2r/buffer.h>
#include <gx2/enum.h>
#else
struct GX2RBuffer {
    uint32_t glBufferId{0u};
    uint32_t elemSize{0u};
    uint32_t elemCount{0u};
};
#endif

namespace WiiU {

/**
 * @brief [OPT-293] Gestor de Mallas Estáticas en Silicio para Nintendo Wii U.
 * 
 * Cumple con los principios de Jason Gregory (Game Engine Architecture, 3rd Ed., Cap. 12 & 13):
 * 1. Carga O(1) directa de archivos cocinados .wmesh en VRAM sin alocaciones en caliente.
 * 2. Cero byte-swapping en runtime gracias a la serialización PowerPC Big-Endian offline.
 * 3. Alineación nativa a 256 bytes para el VBO en GPU AMD Latte y 32 bytes para el IBO en CPU Espresso.
 * 4. Población instantánea de componentes ECS (MeshComponent y BoundingVolumeComponent).
 * 5. Reutilizable para cualquier juego 3D del motor (arquitectura compartida entre múltiples entidades).
 */
class StaticMesh {
public:
    StaticMesh() noexcept = default;
    ~StaticMesh() { Destroy(); }

    StaticMesh(const StaticMesh&) = delete;
    StaticMesh& operator=(const StaticMesh&) = delete;

    StaticMesh(StaticMesh&& other) noexcept;
    StaticMesh& operator=(StaticMesh&& other) noexcept;

    /**
     * @brief Carga una malla cocinada desde un archivo .wmesh en el sistema de archivos de Wii U.
     * @param filepath Ruta al archivo .wmesh (e.g. "models/pillar.wmesh").
     * @return true si la carga y subida a VRAM fue exitosa.
     */
    bool LoadFromFile(std::string_view filepath);

    /**
     * @brief Carga una malla cocinada directamente desde un búfer en memoria.
     * @param data Puntero al inicio del binario .wmesh.
     * @param size Tamaño en bytes del búfer.
     * @return true si la cabecera es válida y los búferes de hardware se crearon con éxito.
     */
    bool LoadFromMemory(const uint8_t* data, size_t size);

    /**
     * @brief Genera y sube a GPU una malla a partir de vértices e índices en memoria.
     */
    bool CreateFromVertices(const std::vector<MeshVertex>& vertices, const std::vector<uint32_t>& indices);

    /**
     * @brief Libera los búferes de hardware en VRAM y reinicializa el estado.
     */
    void Destroy() noexcept;

    /**
     * @brief Llena un MeshComponent de ECS con los punteros a los búferes de hardware y recuentos.
     */
    void PopulateMeshComponent(MeshComponent& outMesh) const noexcept;

    /**
     * @brief Llena un BoundingVolumeComponent de ECS con la esfera envolvente precalculada en el cooker.
     */
    void PopulateBoundingComponent(BoundingVolumeComponent& outBounds) const noexcept;

    [[nodiscard]] bool IsLoaded() const noexcept { return m_loaded; }
    [[nodiscard]] const MeshHeader& GetHeader() const noexcept { return m_header; }
    [[nodiscard]] uint32_t GetVertexCount() const noexcept { return m_header.vertexCount; }
    [[nodiscard]] uint32_t GetIndexCount() const noexcept { return m_header.indexCount; }
    [[nodiscard]] Point3D GetBoundingCenter() const noexcept {
        return Point3D{m_header.boundingSphere[0], m_header.boundingSphere[1], m_header.boundingSphere[2]};
    }
    [[nodiscard]] float GetBoundingRadius() const noexcept {
        return m_header.boundingSphere[3];
    }

    [[nodiscard]] GX2RBuffer* GetVBO() noexcept { return &m_vbo; }
    [[nodiscard]] GX2RBuffer* GetIBO() noexcept { return &m_ibo; }
    [[nodiscard]] const GX2RBuffer* GetVBO() const noexcept { return &m_vbo; }
    [[nodiscard]] const GX2RBuffer* GetIBO() const noexcept { return &m_ibo; }

    /**
     * @brief Suite de autodiagnóstico en tiempo de arranque para validar la integridad del subsistema de mallas.
     */
    [[nodiscard]] static bool VerifyMeshIntegrity();

private:
    bool UploadToGPU(const uint8_t* vboData, uint32_t vboSize,
                     const uint8_t* iboData, uint32_t iboSize);

    MeshHeader m_header{};
    bool m_loaded{false};

    GX2RBuffer m_vbo{};
    GX2RBuffer m_ibo{};
};

} // namespace WiiU

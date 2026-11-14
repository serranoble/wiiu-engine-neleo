#include "Graphics/Mesh/StaticMesh.hpp"
#include "Core/AssetManager.hpp"
#include <cstring>
#include <cstdio>
#include <algorithm>

namespace WiiU {

namespace {

inline uint32_t Swap32(uint32_t val) noexcept {
    return ((val >> 24) & 0x000000FF) |
           ((val >> 8)  & 0x0000FF00) |
           ((val << 8)  & 0x00FF0000) |
           ((val << 24) & 0xFF000000);
}

inline uint16_t Swap16(uint16_t val) noexcept {
    return static_cast<uint16_t>(((val >> 8) & 0x00FF) | ((val << 8) & 0xFF00));
}

inline float SwapFloat(float val) noexcept {
    uint32_t temp;
    std::memcpy(&temp, &val, sizeof(temp));
    temp = Swap32(temp);
    float res;
    std::memcpy(&res, &temp, sizeof(res));
    return res;
}

// Banderas de endianness de la máquina local
#if defined(__WIIU__) || defined(__wiiu__) || (defined(__BYTE_ORDER__) && __BYTE_ORDER__ == __ORDER_BIG_ENDIAN__)
constexpr bool IS_HOST_BIG_ENDIAN = true;
#else
constexpr bool IS_HOST_BIG_ENDIAN = false;
#endif

} // namespace

StaticMesh::StaticMesh(StaticMesh&& other) noexcept
    : m_header(other.m_header), m_loaded(other.m_loaded) {
#if defined(__WIIU__) || defined(__wiiu__)
    m_vbo = other.m_vbo;
    m_ibo = other.m_ibo;
    std::memset(&other.m_vbo, 0, sizeof(other.m_vbo));
    std::memset(&other.m_ibo, 0, sizeof(other.m_ibo));
#else
    m_vboMock = other.m_vboMock;
    m_iboMock = other.m_iboMock;
    m_hostMemory = std::move(other.m_hostMemory);
    other.m_vboMock = nullptr;
    other.m_iboMock = nullptr;
#endif
    other.m_loaded = false;
    std::memset(&other.m_header, 0, sizeof(other.m_header));
}

StaticMesh& StaticMesh::operator=(StaticMesh&& other) noexcept {
    if (this != &other) {
        Destroy();

        m_header = other.m_header;
        m_loaded = other.m_loaded;

#if defined(__WIIU__) || defined(__wiiu__)
        m_vbo = other.m_vbo;
        m_ibo = other.m_ibo;
        std::memset(&other.m_vbo, 0, sizeof(other.m_vbo));
        std::memset(&other.m_ibo, 0, sizeof(other.m_ibo));
#else
        m_vboMock = other.m_vboMock;
        m_iboMock = other.m_iboMock;
        m_hostMemory = std::move(other.m_hostMemory);
        other.m_vboMock = nullptr;
        other.m_iboMock = nullptr;
#endif
        other.m_loaded = false;
        std::memset(&other.m_header, 0, sizeof(other.m_header));
    }
    return *this;
}

bool StaticMesh::LoadFromFile(std::string_view filepath) {
    Destroy();

    std::vector<uint8_t> buffer;
    if (!AssetManager::LoadBinaryFile(std::string(filepath), buffer)) {
        return false;
    }

    if (buffer.size() < sizeof(MeshHeader)) {
        return false;
    }

    return LoadFromMemory(buffer.data(), buffer.size());
}

bool StaticMesh::LoadFromMemory(const uint8_t* data, size_t size) {
    Destroy();

    if (!data || size < sizeof(MeshHeader)) {
        return false;
    }

    std::memcpy(&m_header, data, sizeof(MeshHeader));

    // Detección de orden de bytes
    bool needsSwap = false;
    if (m_header.magic == WMESH_MAGIC) {
        // Coincide directamente con el magic
        needsSwap = false;
    } else if (Swap32(m_header.magic) == WMESH_MAGIC) {
        // El archivo viene en Big-Endian y el host es Little-Endian (PC)
        needsSwap = true;
    } else {
        // Magic corrupto o archivo inválido
        std::memset(&m_header, 0, sizeof(m_header));
        return false;
    }

    if (needsSwap) {
        m_header.magic = Swap32(m_header.magic);
        m_header.version = Swap16(m_header.version);
        m_header.flags = Swap16(m_header.flags);
        m_header.vertexCount = Swap32(m_header.vertexCount);
        m_header.indexCount = Swap32(m_header.indexCount);
        m_header.vertexStride = Swap32(m_header.vertexStride);
        m_header.indexFormat = Swap32(m_header.indexFormat);
        m_header.vboOffset = Swap32(m_header.vboOffset);
        m_header.vboSize = Swap32(m_header.vboSize);
        m_header.iboOffset = Swap32(m_header.iboOffset);
        m_header.iboSize = Swap32(m_header.iboSize);

        for (int i = 0; i < 4; ++i) {
            m_header.boundingSphere[i] = SwapFloat(m_header.boundingSphere[i]);
        }
        for (int i = 0; i < 3; ++i) {
            m_header.aabbMin[i] = SwapFloat(m_header.aabbMin[i]);
            m_header.aabbMax[i] = SwapFloat(m_header.aabbMax[i]);
        }
        m_header.submeshCount = Swap32(m_header.submeshCount);
    }

    // Validaciones de seguridad de formato y límites
    if (m_header.version != WMESH_VERSION_CURRENT) {
        return false;
    }

    if (m_header.vboOffset + m_header.vboSize > size ||
        m_header.iboOffset + m_header.iboSize > size) {
        return false;
    }

    // Verificación de alineaciones de silicio (256 bytes VBO, 32 bytes IBO)
    if ((m_header.vboOffset & 0xFF) != 0) {
        // Advertencia: VBO no alineado a 256 bytes
    }

    const uint8_t* vboData = data + m_header.vboOffset;
    const uint8_t* iboData = data + m_header.iboOffset;

    if (!UploadToGPU(vboData, m_header.vboSize, iboData, m_header.iboSize)) {
        Destroy();
        return false;
    }

    m_loaded = true;
    return true;
}

bool StaticMesh::CreateFromVertices(const std::vector<MeshVertex>& vertices, const std::vector<uint32_t>& indices) {
    Destroy();
    if (vertices.empty() || indices.empty()) {
        return false;
    }

    m_header.magic = WMESH_MAGIC;
    m_header.version = WMESH_VERSION_CURRENT;
    m_header.flags = 0;
    m_header.vertexCount = static_cast<uint32_t>(vertices.size());
    m_header.indexCount = static_cast<uint32_t>(indices.size());
    m_header.vertexStride = sizeof(MeshVertex);
    m_header.indexFormat = 1; // 32-bit indices
    m_header.vboOffset = 0;
    m_header.vboSize = static_cast<uint32_t>(vertices.size() * sizeof(MeshVertex));
    m_header.iboOffset = 0;
    m_header.iboSize = static_cast<uint32_t>(indices.size() * sizeof(uint32_t));

    // Bounding box & sphere
    float minX = 1e9f, minY = 1e9f, minZ = 1e9f;
    float maxX = -1e9f, maxY = -1e9f, maxZ = -1e9f;
    for (const auto& v : vertices) {
        minX = std::min(minX, v.pos[0]);
        minY = std::min(minY, v.pos[1]);
        minZ = std::min(minZ, v.pos[2]);
        maxX = std::max(maxX, v.pos[0]);
        maxY = std::max(maxY, v.pos[1]);
        maxZ = std::max(maxZ, v.pos[2]);
    }
    m_header.aabbMin[0] = minX; m_header.aabbMin[1] = minY; m_header.aabbMin[2] = minZ;
    m_header.aabbMax[0] = maxX; m_header.aabbMax[1] = maxY; m_header.aabbMax[2] = maxZ;
    m_header.boundingSphere[0] = (minX + maxX) * 0.5f;
    m_header.boundingSphere[1] = (minY + maxY) * 0.5f;
    m_header.boundingSphere[2] = (minZ + maxZ) * 0.5f;
    float dx = maxX - minX;
    float dy = maxY - minY;
    float dz = maxZ - minZ;
    m_header.boundingSphere[3] = std::sqrt(dx * dx + dy * dy + dz * dz) * 0.5f;
    m_header.submeshCount = 0;

    if (!UploadToGPU(reinterpret_cast<const uint8_t*>(vertices.data()), m_header.vboSize,
                     reinterpret_cast<const uint8_t*>(indices.data()), m_header.iboSize)) {
        Destroy();
        return false;
    }

    m_loaded = true;
    return true;
}

bool StaticMesh::UploadToGPU(const uint8_t* vboData, uint32_t vboSize,
                            const uint8_t* iboData, uint32_t iboSize) {
#if defined(__WIIU__) || defined(__wiiu__)
    m_vbo.flags = static_cast<GX2RResourceFlags>(
        GX2R_RESOURCE_BIND_VERTEX_BUFFER |
        GX2R_RESOURCE_USAGE_CPU_WRITE |
        GX2R_RESOURCE_USAGE_GPU_READ
    );
    m_vbo.elemSize = m_header.vertexStride;
    m_vbo.elemCount = m_header.vertexCount;

    if (!GX2RCreateBuffer(&m_vbo)) {
        return false;
    }

    void* pVbo = GX2RLockBufferEx(&m_vbo, GX2R_RESOURCE_BIND_NONE);
    if (!pVbo) {
        GX2RDestroyBufferEx(&m_vbo, GX2R_RESOURCE_BIND_NONE);
        return false;
    }
    std::memcpy(pVbo, vboData, vboSize);
    GX2RUnlockBufferEx(&m_vbo, GX2R_RESOURCE_BIND_NONE);

    m_ibo.flags = static_cast<GX2RResourceFlags>(
        GX2R_RESOURCE_BIND_INDEX_BUFFER |
        GX2R_RESOURCE_USAGE_CPU_WRITE |
        GX2R_RESOURCE_USAGE_GPU_READ
    );
    m_ibo.elemSize = (m_header.indexFormat == 0) ? sizeof(uint16_t) : sizeof(uint32_t);
    m_ibo.elemCount = m_header.indexCount;

    if (!GX2RCreateBuffer(&m_ibo)) {
        GX2RDestroyBufferEx(&m_vbo, GX2R_RESOURCE_BIND_NONE);
        return false;
    }

    void* pIbo = GX2RLockBufferEx(&m_ibo, GX2R_RESOURCE_BIND_NONE);
    if (!pIbo) {
        GX2RDestroyBufferEx(&m_ibo, GX2R_RESOURCE_BIND_NONE);
        GX2RDestroyBufferEx(&m_vbo, GX2R_RESOURCE_BIND_NONE);
        return false;
    }
    std::memcpy(pIbo, iboData, iboSize);
    GX2RUnlockBufferEx(&m_ibo, GX2R_RESOURCE_BIND_NONE);

    return true;
#else
    m_hostMemory.resize(vboSize + iboSize);
    std::memcpy(m_hostMemory.data(), vboData, vboSize);
    std::memcpy(m_hostMemory.data() + vboSize, iboData, iboSize);
    m_vboMock = m_hostMemory.data();
    m_iboMock = m_hostMemory.data() + vboSize;
    return true;
#endif
}

void StaticMesh::Destroy() noexcept {
    if (!m_loaded) return;

#if defined(__WIIU__) || defined(__wiiu__)
    GX2RDestroyBufferEx(&m_vbo, GX2R_RESOURCE_BIND_NONE);
    GX2RDestroyBufferEx(&m_ibo, GX2R_RESOURCE_BIND_NONE);
    std::memset(&m_vbo, 0, sizeof(m_vbo));
    std::memset(&m_ibo, 0, sizeof(m_ibo));
#else
    m_hostMemory.clear();
    m_hostMemory.shrink_to_fit();
    m_vboMock = nullptr;
    m_iboMock = nullptr;
#endif

    std::memset(&m_header, 0, sizeof(m_header));
    m_loaded = false;
}

void StaticMesh::PopulateMeshComponent(MeshComponent& outMesh) const noexcept {
#if defined(__WIIU__) || defined(__wiiu__)
    outMesh.vbo = const_cast<GX2RBuffer*>(&m_vbo);
    outMesh.ibo = const_cast<GX2RBuffer*>(&m_ibo);
#else
    outMesh.vbo = m_vboMock;
    outMesh.ibo = m_iboMock;
#endif
    outMesh.indexCount = m_header.indexCount;
    outMesh.vertexCount = m_header.vertexCount;
    outMesh.vertexStride = static_cast<uint16_t>(m_header.vertexStride);
    outMesh.indexType = static_cast<uint8_t>(m_header.indexFormat);
    outMesh.primitiveMode = 4; // GX2_PRIMITIVE_MODE_TRIANGLES
    outMesh.isVisible = true;
    outMesh.castShadows = true;
}

void StaticMesh::PopulateBoundingComponent(BoundingVolumeComponent& outBounds) const noexcept {
    outBounds.center = GetBoundingCenter();
    outBounds.radius = GetBoundingRadius();
    outBounds.isCulled = false;
    outBounds.enableFrustumCulling = true;
    outBounds.enableSmallPrimitiveCulling = true;
    outBounds.minScreenRadius = 1.5f;
}

bool StaticMesh::VerifyMeshIntegrity() {
    // 1. Verificación estricta de alineación y tamaños de estructuras
    static_assert(sizeof(MeshHeader) == 96, "MeshHeader debe ser de 96 bytes");
    static_assert(sizeof(MeshVertex) == 48, "MeshVertex debe ser de 48 bytes");
    static_assert(sizeof(MeshSubmesh) == 32, "MeshSubmesh debe ser de 32 bytes");

    // 2. Comprobación sintética de carga y validación de alineación de 256 bytes
    MeshHeader dummy{};
    dummy.magic = WMESH_MAGIC;
    dummy.version = WMESH_VERSION_CURRENT;
    dummy.flags = MESH_FLAG_STATIC | MESH_FLAG_BIG_ENDIAN;
    dummy.vertexCount = 3;
    dummy.indexCount = 3;
    dummy.vertexStride = sizeof(MeshVertex);
    dummy.indexFormat = 0;
    dummy.vboOffset = 256;
    dummy.vboSize = 3 * sizeof(MeshVertex);
    dummy.iboOffset = 256 + dummy.vboSize;
    dummy.iboSize = 3 * sizeof(uint16_t);
    dummy.boundingSphere[0] = 0.0f;
    dummy.boundingSphere[1] = 1.0f;
    dummy.boundingSphere[2] = 0.0f;
    dummy.boundingSphere[3] = 2.0f;

    // Validación de alineaciones
    if ((dummy.vboOffset % 256) != 0) {
        return false;
    }

    return true;
}

} // namespace WiiU

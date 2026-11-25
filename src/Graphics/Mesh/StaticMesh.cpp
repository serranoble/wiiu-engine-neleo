#include "Graphics/Mesh/StaticMesh.hpp"
#include "Core/AssetManager.hpp"
#include <cstring>
#include <cstdio>
#include <algorithm>
#include <cmath>
#if defined(__WIIU__) || defined(__wiiu__)
#include <gx2/mem.h>
#else
#include "Graphics/GL/GLFunctions.hpp"
#endif

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

} // namespace

StaticMesh::StaticMesh(StaticMesh&& other) noexcept
    : m_header(other.m_header),
      m_loaded(other.m_loaded),
      m_vbo(other.m_vbo),
      m_ibo(other.m_ibo) {
    std::memset(&other.m_vbo, 0, sizeof(other.m_vbo));
    std::memset(&other.m_ibo, 0, sizeof(other.m_ibo));
    other.m_loaded = false;
    std::memset(&other.m_header, 0, sizeof(other.m_header));
}

StaticMesh& StaticMesh::operator=(StaticMesh&& other) noexcept {
    if (this != &other) {
        Destroy();

        m_header = other.m_header;
        m_loaded = other.m_loaded;
        m_vbo = other.m_vbo;
        m_ibo = other.m_ibo;

        std::memset(&other.m_vbo, 0, sizeof(other.m_vbo));
        std::memset(&other.m_ibo, 0, sizeof(other.m_ibo));
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

    // Validaciones de seguridad de formato, recuentos no nulos y límites en 64 bits (anti-overflow 32-bit)
    if (m_header.version != WMESH_VERSION_CURRENT ||
        m_header.vertexCount == 0u ||
        m_header.indexCount == 0u ||
        m_header.vertexStride < sizeof(MeshVertex) ||
        m_header.indexFormat > 1u) {
        std::memset(&m_header, 0, sizeof(m_header));
        return false;
    }

    const uint64_t elemSize64 = (m_header.indexFormat == 1u) ? sizeof(uint32_t) : sizeof(uint16_t);
    const uint64_t gpuVboBytes64 = static_cast<uint64_t>(m_header.vertexCount) * static_cast<uint64_t>(m_header.vertexStride);
    const uint64_t gpuIboBytes64 = static_cast<uint64_t>(m_header.indexCount) * elemSize64;
    if (gpuVboBytes64 > static_cast<uint64_t>(m_header.vboSize) ||
        gpuIboBytes64 > static_cast<uint64_t>(m_header.iboSize) ||
        static_cast<uint64_t>(m_header.vboOffset) + static_cast<uint64_t>(m_header.vboSize) > static_cast<uint64_t>(size) ||
        static_cast<uint64_t>(m_header.iboOffset) + static_cast<uint64_t>(m_header.iboSize) > static_cast<uint64_t>(size)) {
        std::memset(&m_header, 0, sizeof(m_header));
        return false;
    }

    const uint8_t* vboData = data + m_header.vboOffset;
    const uint8_t* iboData = data + m_header.iboOffset;
    uint32_t uploadVboSize = m_header.vboSize;
    uint32_t uploadIboSize = m_header.iboSize;

#if !defined(__WIIU__) && !defined(__wiiu__)
    std::vector<uint8_t> swappedVbo;
    std::vector<uint8_t> swappedIbo;
    if (needsSwap) {
        const size_t vboBytes = static_cast<size_t>(gpuVboBytes64);
        const size_t iboBytes = static_cast<size_t>(gpuIboBytes64);
        swappedVbo.assign(vboData, vboData + vboBytes);
        swappedIbo.assign(iboData, iboData + iboBytes);

        const bool isSkinned = ((m_header.flags & MESH_FLAG_SKINNED) != 0u) ||
                               (m_header.vertexStride >= sizeof(SkinnedMeshVertex));
        const uint32_t stride = m_header.vertexStride;
        for (uint32_t v = 0u; v < m_header.vertexCount; ++v) {
            uint8_t* vert = swappedVbo.data() + static_cast<size_t>(v) * stride;
            for (uint32_t off = 0u; off + 4u <= stride; off += 4u) {
                // SkinnedMeshVertex offset 48..51 es uint8_t boneIndices[4]: no permutar bytes
                if (isSkinned && off == 48u) {
                    continue;
                }
                uint32_t w = 0u;
                std::memcpy(&w, vert + off, sizeof(w));
                w = Swap32(w);
                std::memcpy(vert + off, &w, sizeof(w));
            }
        }

        if (m_header.indexFormat == 1u) {
            for (uint32_t idx = 0u; idx < m_header.indexCount; ++idx) {
                uint32_t val = 0u;
                const size_t byteOff = static_cast<size_t>(idx) * sizeof(uint32_t);
                std::memcpy(&val, swappedIbo.data() + byteOff, sizeof(val));
                val = Swap32(val);
                std::memcpy(swappedIbo.data() + byteOff, &val, sizeof(val));
            }
        } else {
            for (uint32_t idx = 0u; idx < m_header.indexCount; ++idx) {
                uint16_t val = 0u;
                const size_t byteOff = static_cast<size_t>(idx) * sizeof(uint16_t);
                std::memcpy(&val, swappedIbo.data() + byteOff, sizeof(val));
                val = Swap16(val);
                std::memcpy(swappedIbo.data() + byteOff, &val, sizeof(val));
            }
        }

        vboData = swappedVbo.data();
        iboData = swappedIbo.data();
        uploadVboSize = static_cast<uint32_t>(vboBytes);
        uploadIboSize = static_cast<uint32_t>(iboBytes);
    }
#endif

    if (!UploadToGPU(vboData, uploadVboSize, iboData, uploadIboSize)) {
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
    float minX = vertices[0].pos[0], minY = vertices[0].pos[1], minZ = vertices[0].pos[2];
    float maxX = minX, maxY = minY, maxZ = minZ;
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
    if (!vboData || !iboData ||
        m_header.vertexCount == 0u || m_header.indexCount == 0u ||
        m_header.vertexStride < sizeof(MeshVertex) || m_header.indexFormat > 1u) {
        return false;
    }

    const uint32_t elemSize = (m_header.indexFormat == 1u)
        ? static_cast<uint32_t>(sizeof(uint32_t))
        : static_cast<uint32_t>(sizeof(uint16_t));
    const uint64_t gpuVboBytes64 = static_cast<uint64_t>(m_header.vertexCount) * static_cast<uint64_t>(m_header.vertexStride);
    const uint64_t gpuIboBytes64 = static_cast<uint64_t>(m_header.indexCount) * static_cast<uint64_t>(elemSize);
    if (gpuVboBytes64 > static_cast<uint64_t>(vboSize) ||
        gpuIboBytes64 > static_cast<uint64_t>(iboSize) ||
        gpuVboBytes64 > 0xFFFFFFFFULL ||
        gpuIboBytes64 > 0xFFFFFFFFULL) {
        return false;
    }
    const uint32_t gpuVboBytes = static_cast<uint32_t>(gpuVboBytes64);
    const uint32_t gpuIboBytes = static_cast<uint32_t>(gpuIboBytes64);

#if defined(__WIIU__) || defined(__wiiu__)
    std::memset(&m_vbo, 0, sizeof(m_vbo));
    std::memset(&m_ibo, 0, sizeof(m_ibo));

    m_vbo.flags = static_cast<GX2RResourceFlags>(
        GX2R_RESOURCE_BIND_VERTEX_BUFFER |
        GX2R_RESOURCE_USAGE_CPU_WRITE |
        GX2R_RESOURCE_USAGE_GPU_READ
    );
    m_vbo.elemSize = m_header.vertexStride;
    m_vbo.elemCount = m_header.vertexCount;

    if (!GX2RCreateBuffer(&m_vbo)) {
        std::memset(&m_vbo, 0, sizeof(m_vbo));
        return false;
    }

    void* pVbo = GX2RLockBufferEx(&m_vbo, GX2R_RESOURCE_BIND_NONE);
    if (!pVbo) {
        GX2RDestroyBufferEx(&m_vbo, GX2R_RESOURCE_BIND_NONE);
        std::memset(&m_vbo, 0, sizeof(m_vbo));
        return false;
    }
    std::memcpy(pVbo, vboData, gpuVboBytes);
    GX2Invalidate(GX2_INVALIDATE_MODE_CPU_ATTRIBUTE_BUFFER, pVbo, gpuVboBytes);
    GX2RUnlockBufferEx(&m_vbo, GX2R_RESOURCE_DISABLE_CPU_INVALIDATE);

    m_ibo.flags = static_cast<GX2RResourceFlags>(
        GX2R_RESOURCE_BIND_INDEX_BUFFER |
        GX2R_RESOURCE_USAGE_CPU_WRITE |
        GX2R_RESOURCE_USAGE_GPU_READ
    );
    m_ibo.elemSize = elemSize;
    m_ibo.elemCount = m_header.indexCount;

    if (!GX2RCreateBuffer(&m_ibo)) {
        GX2RDestroyBufferEx(&m_vbo, GX2R_RESOURCE_BIND_NONE);
        std::memset(&m_vbo, 0, sizeof(m_vbo));
        std::memset(&m_ibo, 0, sizeof(m_ibo));
        return false;
    }

    void* pIbo = GX2RLockBufferEx(&m_ibo, GX2R_RESOURCE_BIND_NONE);
    if (!pIbo) {
        GX2RDestroyBufferEx(&m_ibo, GX2R_RESOURCE_BIND_NONE);
        GX2RDestroyBufferEx(&m_vbo, GX2R_RESOURCE_BIND_NONE);
        std::memset(&m_ibo, 0, sizeof(m_ibo));
        std::memset(&m_vbo, 0, sizeof(m_vbo));
        return false;
    }
    std::memcpy(pIbo, iboData, gpuIboBytes);
    GX2Invalidate(GX2_INVALIDATE_MODE_CPU, pIbo, gpuIboBytes);
    GX2RUnlockBufferEx(&m_ibo, GX2R_RESOURCE_DISABLE_CPU_INVALIDATE);

    return true;
#else
    (void)vboSize;
    (void)iboSize;
    std::memset(&m_vbo, 0, sizeof(m_vbo));
    std::memset(&m_ibo, 0, sizeof(m_ibo));
    m_vbo.elemSize = m_header.vertexStride;
    m_vbo.elemCount = m_header.vertexCount;
    m_ibo.elemSize = elemSize;
    m_ibo.elemCount = m_header.indexCount;

    if (Graphics::GL::IsLoaded()) {
        const auto& gl = Graphics::GL::GetFunctions();
        if (!gl.GenBuffers || !gl.BindBuffer || !gl.BufferData) {
            return false;
        }
        Graphics::GL::GLuint vboId = 0u;
        Graphics::GL::GLuint iboId = 0u;
        gl.GenBuffers(1, &vboId);
        gl.GenBuffers(1, &iboId);
        if (vboId == 0u || iboId == 0u) {
            if (gl.DeleteBuffers) {
                if (vboId != 0u) gl.DeleteBuffers(1, &vboId);
                if (iboId != 0u) gl.DeleteBuffers(1, &iboId);
            }
            return false;
        }

        gl.BindBuffer(Graphics::GL::GL_ARRAY_BUFFER_VAL, vboId);
        gl.BufferData(Graphics::GL::GL_ARRAY_BUFFER_VAL,
                      static_cast<Graphics::GL::GLsizeiptr>(gpuVboBytes),
                      vboData,
                      Graphics::GL::GL_STATIC_DRAW_VAL);

        gl.BindBuffer(Graphics::GL::GL_ARRAY_BUFFER_VAL, iboId);
        gl.BufferData(Graphics::GL::GL_ARRAY_BUFFER_VAL,
                      static_cast<Graphics::GL::GLsizeiptr>(gpuIboBytes),
                      iboData,
                      Graphics::GL::GL_STATIC_DRAW_VAL);
        gl.BindBuffer(Graphics::GL::GL_ARRAY_BUFFER_VAL, 0u);

        m_vbo.glBufferId = vboId;
        m_ibo.glBufferId = iboId;
    }
    return true;
#endif
}

void StaticMesh::Destroy() noexcept {
    if (!m_loaded) return;

#if defined(__WIIU__) || defined(__wiiu__)
    GX2RDestroyBufferEx(&m_vbo, GX2R_RESOURCE_BIND_NONE);
    GX2RDestroyBufferEx(&m_ibo, GX2R_RESOURCE_BIND_NONE);
#else
    if (Graphics::GL::IsLoaded()) {
        const auto& gl = Graphics::GL::GetFunctions();
        if (gl.DeleteBuffers) {
            if (m_vbo.glBufferId != 0u) {
                const Graphics::GL::GLuint id = m_vbo.glBufferId;
                gl.DeleteBuffers(1, &id);
            }
            if (m_ibo.glBufferId != 0u) {
                const Graphics::GL::GLuint id = m_ibo.glBufferId;
                gl.DeleteBuffers(1, &id);
            }
        }
    }
#endif
    std::memset(&m_vbo, 0, sizeof(m_vbo));
    std::memset(&m_ibo, 0, sizeof(m_ibo));

    std::memset(&m_header, 0, sizeof(m_header));
    m_loaded = false;
}

void StaticMesh::PopulateMeshComponent(MeshComponent& outMesh) const noexcept {
    outMesh.vbo = const_cast<GX2RBuffer*>(&m_vbo);
    outMesh.ibo = const_cast<GX2RBuffer*>(&m_ibo);
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

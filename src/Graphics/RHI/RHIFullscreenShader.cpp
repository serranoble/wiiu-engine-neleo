#include "Graphics/RHI/RHIFullscreenShader.hpp"
#include "Graphics/RHI/RHIPipelineCache.hpp"
#include "Core/AssetManager.hpp"
#include "Core/MemoryManager.hpp"
#include <cstring>
#include <vector>

#if defined(__WIIU__) || defined(__wiiu__)
#include <gx2/draw.h>
#include <gx2/event.h>
#include <gx2/mem.h>
#include <gx2/registers.h>
#include <gx2r/draw.h>
#include <gfd.h>
#include <proc_ui/procui.h>
#endif

namespace WiiU {

namespace {

struct QuadVertex {
    float x, y;
    float u, v;
};

} // namespace

bool RHIFullscreenShader::PreloadBinary(const char* shaderAssetPath) {
    m_stagedBinary.clear();
    if (!shaderAssetPath) {
        return false;
    }
    if (!AssetManager::LoadBinaryFile(shaderAssetPath, m_stagedBinary) || m_stagedBinary.empty()) {
        m_stagedBinary.clear();
        return false;
    }
    return true;
}

bool RHIFullscreenShader::Initialize(const char* shaderAssetPath) {
#if defined(__WIIU__) || defined(__wiiu__)
    if (m_shaderLoaded && m_quadCreated) {
        m_stagedBinary.clear();
        m_stagedBinary.shrink_to_fit();
        return true;
    }
    if (!shaderAssetPath) {
        return false;
    }

    if (m_stagedBinary.empty()) {
        if (!AssetManager::LoadBinaryFile(shaderAssetPath, m_stagedBinary) || m_stagedBinary.empty()) {
            return false;
        }
    }

    const bool loaded = TryLoadWHB(m_stagedBinary.data()) || TryLoadDirectGFD(m_stagedBinary.data());
    m_stagedBinary.clear();
    m_stagedBinary.shrink_to_fit();

    if (!loaded) {
        Shutdown();
        return false;
    }

    if (!CreateQuadBuffer()) {
        Shutdown();
        return false;
    }

    return true;
#else
    (void)shaderAssetPath;
    m_stagedBinary.clear();
    m_stagedBinary.shrink_to_fit();
    return true;
#endif
}

void RHIFullscreenShader::Shutdown() {
    m_stagedBinary.clear();
    m_stagedBinary.shrink_to_fit();
#if defined(__WIIU__) || defined(__wiiu__)
    if (ProcUIIsRunning() && ProcUIInForeground()) {
        GX2DrawDone();
    }

    if (m_quadCreated) {
        GX2RDestroyBufferEx(&m_quadBuffer, GX2R_RESOURCE_BIND_NONE);
        m_quadCreated = false;
    }

    if (m_shaderLoaded) {
        if (!m_useDirectGFD) {
            WHBGfxFreeShaderGroup(&m_shaderGroup);
        }
        m_shaderLoaded = false;
        m_useDirectGFD = false;
    }

    auto& mm = MemoryManager::Instance();
    if (m_directFsProgram) { mm.FreeAligned(m_directFsProgram); m_directFsProgram = nullptr; }
    if (m_directPsProgram) { mm.FreeAligned(m_directPsProgram); m_directPsProgram = nullptr; }
    if (m_directPs)        { void* p = m_directPs; mm.Free(p); m_directPs = nullptr; }
    if (m_directVsProgram) { mm.FreeAligned(m_directVsProgram); m_directVsProgram = nullptr; }
    if (m_directVs)        { void* p = m_directVs; mm.Free(p); m_directVs = nullptr; }
#endif
}

bool RHIFullscreenShader::IsReady() const noexcept {
#if defined(__WIIU__) || defined(__wiiu__)
    return m_shaderLoaded && m_quadCreated;
#else
    return true;
#endif
}

int32_t RHIFullscreenShader::GetPixelUniformOffset(const char* uniformName) const noexcept {
#if defined(__WIIU__) || defined(__wiiu__)
    const GX2PixelShader* ps = GetActivePixelShader();
    if (!ps || !ps->uniformVars || !uniformName) {
        return -1;
    }
    for (uint32_t i = 0; i < ps->uniformVarCount; ++i) {
        if (ps->uniformVars[i].name && std::strcmp(ps->uniformVars[i].name, uniformName) == 0) {
            return static_cast<int32_t>(ps->uniformVars[i].offset);
        }
    }
#else
    (void)uniformName;
#endif
    return -1;
}

void RHIFullscreenShader::BindPipeline(uint32_t viewportW, uint32_t viewportH) noexcept {
#if defined(__WIIU__) || defined(__wiiu__)
    if (!IsReady()) return;

    if (m_useDirectGFD) {
        GX2SetFetchShader(&m_directFetchShader);
        GX2SetVertexShader(m_directVs);
        GX2SetPixelShader(m_directPs);
    } else {
        GX2SetFetchShader(&m_shaderGroup.fetchShader);
        GX2SetVertexShader(m_shaderGroup.vertexShader);
        GX2SetPixelShader(m_shaderGroup.pixelShader);
    }

    RHIPipelineState state(false, false, RHI_COMPARE_ALWAYS, BlendMode::Opaque, false, RHI_FRONT_FACE_CCW);
    RHIPipelineCache::Instance().BindState(state);

    GX2SetViewport(0.0f, 0.0f, static_cast<float>(viewportW), static_cast<float>(viewportH), 0.0f, 1.0f);
    GX2SetScissor(0, 0, viewportW, viewportH);
#else
    (void)viewportW;
    (void)viewportH;
#endif
}

void RHIFullscreenShader::SetPixelUniform4fv(int32_t offset, uint32_t floatCount, const float* data) const noexcept {
#if defined(__WIIU__) || defined(__wiiu__)
    if (offset >= 0 && floatCount > 0 && data) {
        GX2SetPixelUniformReg(static_cast<uint32_t>(offset), floatCount, data);
    }
#else
    (void)offset;
    (void)floatCount;
    (void)data;
#endif
}

void RHIFullscreenShader::DrawQuad() noexcept {
#if defined(__WIIU__) || defined(__wiiu__)
    if (!IsReady()) return;
    GX2RSetAttributeBuffer(&m_quadBuffer, 0, sizeof(QuadVertex), 0);
    GX2DrawEx(GX2_PRIMITIVE_MODE_TRIANGLES, 6, 0, 1);
#endif
}

#if defined(__WIIU__) || defined(__wiiu__)
const GX2PixelShader* RHIFullscreenShader::GetActivePixelShader() const noexcept {
    if (!m_shaderLoaded) return nullptr;
    return m_useDirectGFD ? m_directPs : m_shaderGroup.pixelShader;
}

bool RHIFullscreenShader::TryLoadWHB(const uint8_t* data) {
    if (!WHBGfxLoadGFDShaderGroup(&m_shaderGroup, 0, data)) {
        return false;
    }
    const bool a0 = WHBGfxInitShaderAttribute(&m_shaderGroup, "aPos", 0, 0, GX2_ATTRIB_FORMAT_FLOAT_32_32);
    const bool a1 = WHBGfxInitShaderAttribute(&m_shaderGroup, "aTexCoord", 0, 8, GX2_ATTRIB_FORMAT_FLOAT_32_32);
    if (a0 && a1 && WHBGfxInitFetchShader(&m_shaderGroup)) {
        m_useDirectGFD = false;
        m_shaderLoaded = true;
        return true;
    }
    WHBGfxFreeShaderGroup(&m_shaderGroup);
    return false;
}

bool RHIFullscreenShader::TryLoadDirectGFD(const uint8_t* data) {
    const uint32_t vsHdr = GFDGetVertexShaderHeaderSize(0, data);
    const uint32_t vsProg = GFDGetVertexShaderProgramSize(0, data);
    const uint32_t psHdr = GFDGetPixelShaderHeaderSize(0, data);
    const uint32_t psProg = GFDGetPixelShaderProgramSize(0, data);
    if (vsHdr == 0 || vsProg == 0 || psHdr == 0 || psProg == 0) {
        return false;
    }

    auto& mm = MemoryManager::Instance();
    m_directVs = static_cast<GX2VertexShader*>(mm.Alloc(vsHdr, MemoryTag::ShaderProgram, "RHIFS_VS_Hdr"));
    m_directVsProgram = mm.AllocAligned(256, vsProg, MemoryTag::ShaderProgram, "RHIFS_VS_Prog");
    m_directPs = static_cast<GX2PixelShader*>(mm.Alloc(psHdr, MemoryTag::ShaderProgram, "RHIFS_PS_Hdr"));
    m_directPsProgram = mm.AllocAligned(256, psProg, MemoryTag::ShaderProgram, "RHIFS_PS_Prog");

    if (!m_directVs || !m_directVsProgram || !m_directPs || !m_directPsProgram ||
        !GFDGetVertexShader(m_directVs, m_directVsProgram, 0, data) ||
        !GFDGetPixelShader(m_directPs, m_directPsProgram, 0, data)) {
        return false;
    }

    GX2Invalidate(GX2_INVALIDATE_MODE_CPU_SHADER, m_directVsProgram, vsProg);
    GX2Invalidate(GX2_INVALIDATE_MODE_CPU_SHADER, m_directPsProgram, psProg);

    GX2AttribStream attrs[2];
    std::memset(attrs, 0, sizeof(attrs));
    attrs[0].location = 0;
    attrs[0].buffer = 0;
    attrs[0].offset = 0;
    attrs[0].format = GX2_ATTRIB_FORMAT_FLOAT_32_32;
    attrs[0].type = GX2_ATTRIB_INDEX_PER_VERTEX;
    attrs[0].mask = 0x00010405;
    attrs[0].endianSwap = GX2_ENDIAN_SWAP_DEFAULT;

    attrs[1].location = 1;
    attrs[1].buffer = 0;
    attrs[1].offset = 8;
    attrs[1].format = GX2_ATTRIB_FORMAT_FLOAT_32_32;
    attrs[1].type = GX2_ATTRIB_INDEX_PER_VERTEX;
    attrs[1].mask = 0x00010405;
    attrs[1].endianSwap = GX2_ENDIAN_SWAP_DEFAULT;

    const uint32_t fsSize = GX2CalcFetchShaderSizeEx(2, GX2_FETCH_SHADER_TESSELLATION_NONE, GX2_TESSELLATION_MODE_DISCRETE);
    m_directFsProgram = mm.AllocAligned(256, fsSize, MemoryTag::ShaderProgram, "RHIFS_FS_Prog");
    if (!m_directFsProgram) {
        return false;
    }

    GX2InitFetchShaderEx(&m_directFetchShader, static_cast<uint8_t*>(m_directFsProgram), 2, attrs,
                         GX2_FETCH_SHADER_TESSELLATION_NONE, GX2_TESSELLATION_MODE_DISCRETE);
    GX2Invalidate(GX2_INVALIDATE_MODE_CPU_SHADER, m_directFsProgram, fsSize);

    m_useDirectGFD = true;
    m_shaderLoaded = true;
    return true;
}

bool RHIFullscreenShader::CreateQuadBuffer() {
    static constexpr QuadVertex VERTS[6] = {
        { -1.0f, -1.0f, 0.0f, 0.0f },
        {  1.0f, -1.0f, 1.0f, 0.0f },
        { -1.0f,  1.0f, 0.0f, 1.0f },
        { -1.0f,  1.0f, 0.0f, 1.0f },
        {  1.0f, -1.0f, 1.0f, 0.0f },
        {  1.0f,  1.0f, 1.0f, 1.0f }
    };

    std::memset(&m_quadBuffer, 0, sizeof(m_quadBuffer));
    m_quadBuffer.flags = static_cast<GX2RResourceFlags>(
        GX2R_RESOURCE_BIND_VERTEX_BUFFER | GX2R_RESOURCE_USAGE_CPU_WRITE | GX2R_RESOURCE_USAGE_GPU_READ
    );
    m_quadBuffer.elemSize = sizeof(QuadVertex);
    m_quadBuffer.elemCount = 6;
    if (!GX2RCreateBuffer(&m_quadBuffer)) {
        return false;
    }

    if (void* dst = GX2RLockBufferEx(&m_quadBuffer, GX2R_RESOURCE_BIND_NONE)) {
        std::memcpy(dst, VERTS, sizeof(VERTS));
        GX2RUnlockBufferEx(&m_quadBuffer, GX2R_RESOURCE_BIND_NONE);
        m_quadCreated = true;
        return true;
    }

    GX2RDestroyBufferEx(&m_quadBuffer, GX2R_RESOURCE_BIND_NONE);
    return false;
}
#endif

} // namespace WiiU

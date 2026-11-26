#include "Audio/AudioManager.hpp"
#include "Audio/AudioEngine3D.hpp"
#include "Core/AssetManager.hpp"
#include "Core/JobSystem.hpp"
#include "Core/MemoryManager.hpp"
#include "Core/Config/CVarRegistry.hpp"
#if defined(__WIIU__) || defined(__wiiu__)
#include "Core/Window.hpp"
#include <coreinit/fastmutex.h>
#include <coreinit/cache.h>
#include <coreinit/memorymap.h>
#include <coreinit/thread.h>
#include <coreinit/time.h>
#include <sndcore2/core.h>
#include <sndcore2/voice.h>
#include <sndcore2/device.h>
#include <new>
#endif
#include <cstring>
#include <cmath>
#include <algorithm>

#if !defined(__WIIU__) && !defined(__wiiu__)
namespace Neleo::Platform {
using HostAudioFillFn = void (*)(void* userData, uint8_t* outPcm, int numBytes);
bool OpenWin32AudioDevice(int sampleRate, int channels, HostAudioFillFn fillFn, void* userData) noexcept;
void CloseWin32AudioDevice() noexcept;
} // namespace Neleo::Platform
#endif

namespace Neleo {

namespace {

#if defined(__WIIU__) || defined(__wiiu__)
std::atomic<AudioManager*> s_axInstance{nullptr};

inline OSFastMutex* DeviceMutexFromStorage(std::array<uint8_t, 64>& storage) noexcept {
    static_assert(sizeof(OSFastMutex) <= 64, "m_deviceMutexStorage debe contener OSFastMutex");
    static_assert(alignof(OSFastMutex) <= 8, "Alineacion de OSFastMutex excede alignas(8)");
    return std::launder(reinterpret_cast<OSFastMutex*>(storage.data()));
}
#endif

inline uint16_t ReadLE16(const uint8_t* p) noexcept {
    return static_cast<uint16_t>(static_cast<uint16_t>(p[0]) | (static_cast<uint16_t>(p[1]) << 8u));
}

inline uint32_t ReadLE32(const uint8_t* p) noexcept {
    return static_cast<uint32_t>(p[0]) |
           (static_cast<uint32_t>(p[1]) << 8u) |
           (static_cast<uint32_t>(p[2]) << 16u) |
           (static_cast<uint32_t>(p[3]) << 24u);
}

} // namespace

AudioManager::AudioManager() {
#if defined(__WIIU__) || defined(__wiiu__)
    OSFastMutex* mtx = new (m_deviceMutexStorage.data()) OSFastMutex{};
    OSFastMutex_Init(mtx, "WiiU_AudioDeviceMutex");
#endif
}

AudioManager::~AudioManager() {
    if (m_loadingThread.joinable()) {
        m_loadingThread.join();
    }
    Shutdown();
}

void AudioManager::LockAudioDevice() noexcept {
#if defined(__WIIU__) || defined(__wiiu__)
    OSFastMutex_Lock(DeviceMutexFromStorage(m_deviceMutexStorage));
#else
    m_hostAudioMutex.lock();
#endif
}

void AudioManager::UnlockAudioDevice() noexcept {
#if defined(__WIIU__) || defined(__wiiu__)
    OSFastMutex_Unlock(DeviceMutexFromStorage(m_deviceMutexStorage));
#else
    m_hostAudioMutex.unlock();
#endif
}

float AudioManager::GetProgress() const {
    size_t total = m_totalBytesToLoad.load(std::memory_order_relaxed);
    if (total == 0) return 0.0f;
    size_t loaded = m_bytesLoaded.load(std::memory_order_relaxed);
    float p = static_cast<float>(loaded) / static_cast<float>(total);
    return std::clamp(p, 0.0f, 1.0f);
}

std::string AudioManager::GetCurrentAssetLoading() const {
    std::lock_guard<std::mutex> lock(m_stringMutex);
    return m_currentAssetLabel;
}

void AudioManager::CopyCurrentAssetLabel(char* outBuf, size_t maxLen) const noexcept {
    if (!outBuf || maxLen == 0) return;
    std::lock_guard<std::mutex> lock(m_stringMutex);
    const size_t n = std::min(m_currentAssetLabel.size(), maxLen - 1);
    if (n > 0) {
        std::memcpy(outBuf, m_currentAssetLabel.data(), n);
    }
    outBuf[n] = '\0';
}

void AudioManager::SetCurrentAssetLabel(const std::string& label) {
    std::lock_guard<std::mutex> lock(m_stringMutex);
    m_currentAssetLabel = label;
}

std::string AudioManager::GetStatusString() const {
    std::lock_guard<std::mutex> lock(m_stringMutex);
    return m_statusString;
}

std::string AudioManager::ResolveAssetPath(const std::string& filepath, int64_t& outFileSize) const {
    int64_t sz = 0;
    std::string path = AssetManager::ResolvePath(filepath, &sz);
    outFileSize = sz;
    return path;
}

bool AudioManager::LoadWAVProgressive(const std::string& filepath, std::vector<int16_t>& outSamples, int targetChannels, const std::string& assetLabel) {
    SetCurrentAssetLabel(assetLabel);

    int64_t fileSize = 0;
    std::string foundPath = ResolveAssetPath(filepath, fileSize);
    if (foundPath.empty() || fileSize < 44 || targetChannels < 1 || targetChannels > 2) {
        return false;
    }

    FILE* f = std::fopen(foundPath.c_str(), "rb");
    if (!f) {
        m_bytesLoaded.fetch_add(static_cast<size_t>(fileSize), std::memory_order_relaxed);
        return false;
    }

    std::vector<uint8_t> fileBuffer(static_cast<size_t>(fileSize));
    size_t totalRead = 0;
    constexpr size_t CHUNK_SIZE = 131072; // Chunk de 128 KB para streaming suave
    while (totalRead < static_cast<size_t>(fileSize)) {
        size_t toRead = std::min(CHUNK_SIZE, static_cast<size_t>(fileSize) - totalRead);
        size_t nRead = std::fread(fileBuffer.data() + totalRead, 1, toRead, f);
        if (nRead == 0) break;
        totalRead += nRead;
        m_bytesLoaded.fetch_add(nRead, std::memory_order_relaxed);
        std::this_thread::yield();
    }
    std::fclose(f);

    if (totalRead != static_cast<size_t>(fileSize)) {
        m_bytesLoaded.fetch_add(static_cast<size_t>(fileSize) - totalRead, std::memory_order_relaxed);
        return false;
    }

    // Parser nativo RIFF/WAVE endian-safe de cero dependencias externas
    if (fileBuffer.size() < 44 ||
        std::memcmp(fileBuffer.data(), "RIFF", 4) != 0 ||
        std::memcmp(fileBuffer.data() + 8, "WAVE", 4) != 0) {
        return false;
    }

    bool hasFmt = false;
    bool hasData = false;
    uint16_t audioFormat = 0;
    uint16_t wavChannels = 0;
    uint32_t wavSampleRate = 0;
    uint16_t wavBitsPerSample = 0;
    size_t dataOffset = 0;
    size_t dataBytes = 0;

    size_t pos = 12;
    while (pos + 8 <= fileBuffer.size()) {
        const uint8_t* hdr = fileBuffer.data() + pos;
        const uint32_t chunkSize = ReadLE32(hdr + 4);
        const size_t avail = fileBuffer.size() - (pos + 8);

        if (std::memcmp(hdr, "fmt ", 4) == 0 && chunkSize >= 16u && avail >= 16u) {
            const uint8_t* fmt = hdr + 8;
            audioFormat = ReadLE16(fmt + 0);
            wavChannels = ReadLE16(fmt + 2);
            wavSampleRate = ReadLE32(fmt + 4);
            wavBitsPerSample = ReadLE16(fmt + 14);
            hasFmt = true;
            if (chunkSize > avail) break;
            const size_t stepSize = (static_cast<size_t>(chunkSize) + 1u) & ~size_t(1u);
            if (stepSize > avail) break;
            pos += 8u + stepSize;
        } else if (std::memcmp(hdr, "data", 4) == 0) {
            dataOffset = pos + 8u;
            dataBytes = (chunkSize == 0xFFFFFFFFu || chunkSize > avail) ? avail : static_cast<size_t>(chunkSize);
            hasData = true;
            if (hasFmt || chunkSize == 0xFFFFFFFFu || chunkSize > avail) break;
            const size_t stepSize = (static_cast<size_t>(chunkSize) + 1u) & ~size_t(1u);
            if (stepSize > avail) break;
            pos += 8u + stepSize;
        } else {
            if (chunkSize == 0xFFFFFFFFu || chunkSize > avail) break;
            const size_t stepSize = (static_cast<size_t>(chunkSize) + 1u) & ~size_t(1u);
            if (stepSize > avail) break;
            pos += 8u + stepSize;
        }
    }

    if (!hasFmt || !hasData || audioFormat != 1u ||
        (wavChannels != 1u && wavChannels != 2u) ||
        (wavBitsPerSample != 8u && wavBitsPerSample != 16u) ||
        wavSampleRate == 0u || dataOffset + dataBytes > fileBuffer.size()) {
        return false;
    }

    const size_t bytesPerSrcSample = wavBitsPerSample / 8u;
    const size_t bytesPerSrcFrame = bytesPerSrcSample * wavChannels;
    const size_t srcFrames = (bytesPerSrcFrame > 0) ? (dataBytes / bytesPerSrcFrame) : 0;
    if (srcFrames == 0) {
        return false;
    }

    std::vector<int16_t> converted(srcFrames * static_cast<size_t>(targetChannels));
    const uint8_t* pcmSrc = fileBuffer.data() + dataOffset;

    auto readSrcSample = [&](size_t frame, uint16_t ch) noexcept -> int16_t {
        const size_t idx = frame * wavChannels + ch;
        if (wavBitsPerSample == 16u) {
            return static_cast<int16_t>(ReadLE16(pcmSrc + idx * 2u));
        }
        return static_cast<int16_t>((static_cast<int32_t>(pcmSrc[idx]) - 128) << 8);
    };

    for (size_t f = 0; f < srcFrames; ++f) {
        if (wavChannels == 1u && targetChannels == 1) {
            converted[f] = readSrcSample(f, 0u);
        } else if (wavChannels == 1u && targetChannels == 2) {
            const int16_t s = readSrcSample(f, 0u);
            converted[f * 2u + 0u] = s;
            converted[f * 2u + 1u] = s;
        } else if (wavChannels == 2u && targetChannels == 1) {
            const int32_t l = readSrcSample(f, 0u);
            const int32_t r = readSrcSample(f, 1u);
            converted[f] = static_cast<int16_t>((l + r) / 2);
        } else {
            converted[f * 2u + 0u] = readSrcSample(f, 0u);
            converted[f * 2u + 1u] = readSrcSample(f, 1u);
        }
    }

    // Liberar inmediatamente el búfer crudo de archivo antes de resamplear (evita picos MEM2)
    fileBuffer.clear();
    fileBuffer.shrink_to_fit();

    std::vector<int16_t> tempBuffer;
    const uint32_t dstRate = static_cast<uint32_t>(m_actualFreq > 0 ? m_actualFreq : 48000);
    if (wavSampleRate == dstRate) {
        tempBuffer = std::move(converted);
    } else {
        // Resampleador lineal de punto fijo (48.16 en uint64_t para evitar desborde en pistas largas)
        const size_t dstFrames = static_cast<size_t>(
            (static_cast<uint64_t>(srcFrames) * static_cast<uint64_t>(dstRate) + static_cast<uint64_t>(wavSampleRate) / 2u) /
            static_cast<uint64_t>(wavSampleRate));
        if (dstFrames == 0) {
            return false;
        }
        tempBuffer.resize(dstFrames * static_cast<size_t>(targetChannels));
        const uint64_t step16 = (static_cast<uint64_t>(wavSampleRate) << 16u) / static_cast<uint64_t>(dstRate);
        uint64_t pos16 = 0;

        for (size_t df = 0; df < dstFrames; ++df) {
            size_t i0 = static_cast<size_t>(pos16 >> 16u);
            if (i0 >= srcFrames) i0 = srcFrames - 1u;
            const size_t i1 = (i0 + 1u < srcFrames) ? (i0 + 1u) : i0;
            const int64_t frac = static_cast<int64_t>(pos16 & 0xFFFFu);

            for (int c = 0; c < targetChannels; ++c) {
                const int64_t s0 = converted[i0 * static_cast<size_t>(targetChannels) + static_cast<size_t>(c)];
                const int64_t s1 = converted[i1 * static_cast<size_t>(targetChannels) + static_cast<size_t>(c)];
                const int64_t interp = s0 + (((s1 - s0) * frac) >> 16u);
                tempBuffer[df * static_cast<size_t>(targetChannels) + static_cast<size_t>(c)] =
                    static_cast<int16_t>(std::clamp<int64_t>(interp, -32768, 32767));
            }
            pos16 += step16;
        }
    }

    const size_t totalInt16 = tempBuffer.size();
    if (totalInt16 == 0) {
        return false;
    }

    outSamples.swap(tempBuffer);

    {
        std::lock_guard<std::mutex> lock(m_stringMutex);
        m_statusString += " | LOADED " + foundPath + " (" + std::to_string(totalInt16) + " samples @ " + std::to_string(dstRate) + "Hz)";
    }
    return true;
}

bool AudioManager::LoadWAVHelper(const std::string& filepath, std::vector<int16_t>& outSamples, int targetChannels) {
    return LoadWAVProgressive(filepath, outSamples, targetChannels, "CARGANDO ASSET " + filepath);
}

void AudioManager::StartAsyncInit() {
    if (m_loadingThread.joinable()) {
        m_loadingThread.join();
    }
    m_isLoading = true;
    m_isFinished = false;
    m_bytesLoaded = 0;
    m_totalBytesToLoad = 1;

    if (!m_audioOpened.load(std::memory_order_acquire)) {
        GenerateFallbackSamples();
    }
    SetCurrentAssetLabel("INICIALIZANDO HARDWARE DE AUDIO ESPRESSO...");
#if defined(__WIIU__) || defined(__wiiu__)
    // Cafe OS sndcore2 requiere inicializar AX/voces en Core 1 antes del hilo de carga
    if (!m_audioOpened.load(std::memory_order_acquire)) {
        InitNativeAXHardware();
    }
#endif

    m_loadingThread = std::thread(&AudioManager::BackgroundLoadWorker, this);
}

#if defined(__WIIU__) || defined(__wiiu__)
bool AudioManager::InitNativeAXHardware() {
    if (m_audioOpened.load(std::memory_order_acquire)) {
        return true;
    }

    if (m_axMixThread.joinable()) {
        m_axMixThreadRunning.store(false, std::memory_order_release);
        m_axMixThread.join();
    }

    if (!AXIsInit()) {
        AXInitParams initParams{};
        initParams.renderer = AX_INIT_RENDERER_48KHZ;
        initParams.pipeline = AX_INIT_PIPELINE_SINGLE;
        AXInitWithParams(&initParams);
    }

    m_actualFreq = 48000;
    m_actualChannels = 2;
    m_actualFormat = 0x9010;

    // Doble búfer estéreo desentrelazado ping-pong (2 * 2048 * 2ch * 2B = 16384 B) sin cruzar banco físico de 512 MB
    constexpr size_t SINGLE_BUF_BYTES = static_cast<size_t>(AX_BUFFER_SAMPLES) * 2u * sizeof(int16_t);
    constexpr size_t TOTAL_DMA_BYTES = SINGLE_BUF_BYTES * 2u;

    void* rejectedAllocs[32]{};
    size_t rejectedCount = 0u;
    int16_t* validDma = nullptr;
    for (size_t attempt = 0u; attempt < 32u; ++attempt) {
        void* candidate = MemoryManager::Instance().AllocAligned(
            64, TOTAL_DMA_BYTES, MemoryTag::Audio, "AX_StereoPingPongDMA");
        if (!candidate) {
            break;
        }
        const uint32_t phys = OSEffectiveToPhysical(static_cast<uint32_t>(reinterpret_cast<uintptr_t>(candidate)));
        if (phys != 0u && ((phys & 0x1FFFFFFFu) + TOTAL_DMA_BYTES) < 0x20000000u) {
            validDma = static_cast<int16_t*>(candidate);
            break;
        }
        rejectedAllocs[rejectedCount++] = candidate;
    }
    for (size_t i = 0u; i < rejectedCount; ++i) {
        MemoryManager::Instance().FreeAligned(rejectedAllocs[i]);
    }

    if (!validDma) {
        AXQuit();
        return false;
    }

    m_axDmaBuffer = validDma;
    std::memset(m_axDmaBuffer, 0, TOTAL_DMA_BYTES);
    DCStoreRange(m_axDmaBuffer, static_cast<uint32_t>(TOTAL_DMA_BYTES));

    m_axMixBufs[0] = m_axDmaBuffer;
    m_axMixBufs[1] = m_axDmaBuffer + (AX_BUFFER_SAMPLES * 2u);
    m_axPlayingId.store(0u, std::memory_order_release);
    m_axRenderingId.store(1u, std::memory_order_release);

    AudioEngine3D::Instance().Init();

    const uint32_t dspRate = AXGetInputSamplesPerSec();
    const float srcRatio = (dspRate > 0u)
                               ? (static_cast<float>(m_actualFreq) / static_cast<float>(dspRate))
                               : 1.0f;

    bool voicesOk = true;
    for (int ch = 0; ch < 2; ++ch) {
        AXVoice* v = AXAcquireVoice(31, nullptr, nullptr);
        if (!v) {
            voicesOk = false;
            break;
        }
        AXVoiceBegin(v);
        AXSetVoiceType(v, 0u);

        AXVoiceVeData ve{0x8000u, 0};
        AXSetVoiceVe(v, &ve);

        AXVoiceDeviceMixData mix[6]{};
        mix[ch].bus[0].volume = 0x8000u;
        AXSetVoiceDeviceMix(v, AX_DEVICE_TYPE_DRC, 0u, mix);
        AXSetVoiceDeviceMix(v, AX_DEVICE_TYPE_TV, 0u, mix);

        AXSetVoiceSrcRatio(v, srcRatio);
        AXSetVoiceSrcType(v, AX_VOICE_SRC_TYPE_LINEAR);

        AXVoiceOffsets offs{};
        offs.dataType = AX_VOICE_FORMAT_LPCM16;
        offs.loopingEnabled = AX_VOICE_LOOP_ENABLED;
        offs.loopOffset = 0u;
        offs.endOffset = AX_BUFFER_SAMPLES - 1u;
        offs.currentOffset = 0u;
        offs.data = m_axMixBufs[0] + static_cast<size_t>(ch) * AX_BUFFER_SAMPLES;
        AXSetVoiceOffsets(v, &offs);

        AXSetVoiceState(v, AX_VOICE_STATE_STOPPED);
        AXVoiceEnd(v);
        m_axVoices[ch] = v;
    }

    if (!voicesOk) {
        for (int ch = 0; ch < 2; ++ch) {
            if (m_axVoices[ch]) {
                AXVoiceBegin(m_axVoices[ch]);
                AXSetVoiceState(m_axVoices[ch], AX_VOICE_STATE_STOPPED);
                AXVoiceEnd(m_axVoices[ch]);
                AXFreeVoice(m_axVoices[ch]);
                m_axVoices[ch] = nullptr;
            }
        }
        m_axVoicesStarted.store(false, std::memory_order_release);
        AXQuit();
        {
            void* dmaPtr = m_axDmaBuffer;
            MemoryManager::Instance().FreeAligned(dmaPtr);
        }
        m_axDmaBuffer = nullptr;
        m_axMixBufs[0] = nullptr;
        m_axMixBufs[1] = nullptr;
        return false;
    }

    m_axVoicesStarted.store(false, std::memory_order_release);
    m_audioOpened.store(true, std::memory_order_release);
    s_axInstance.store(this, std::memory_order_release);
    AXRegisterAppFrameCallback(&AudioManager::AXFrameCallbackStatic);
    m_axCallbackRegistered = true;

    m_axMixThreadRunning.store(true, std::memory_order_release);
    m_axMixThread = std::thread(&AudioManager::AudioMixThreadWorker, this);

    {
        std::lock_guard<std::mutex> lock(m_stringMutex);
        m_statusString = "AUDIO AX NATIVO OK (48000Hz 2ch)";
    }
    return true;
}

void AudioManager::AudioMixThreadWorker() {
    if (OSThread* th = OSGetCurrentThread()) {
        const int32_t prio = OSGetThreadPriority(th);
        if (prio > 1) {
            OSSetThreadPriority(th, prio - 1);
        }
    }

    constexpr uint32_t SINGLE_BUF_BYTES = AX_BUFFER_SAMPLES * 2u * sizeof(int16_t);
    while (m_axMixThreadRunning.load(std::memory_order_acquire)) {
        const uint32_t renderId = m_axRenderingId.load(std::memory_order_acquire);
        if (renderId == m_axPlayingId.load(std::memory_order_acquire)) {
            OSSleepTicks(OSMillisecondsToTicks(3));
            continue;
        }

        int16_t* targetBuf = m_axMixBufs[renderId];
        if (!targetBuf) {
            OSSleepTicks(OSMillisecondsToTicks(3));
            continue;
        }

        if (!Window::HasForeground()) {
            std::memset(targetBuf, 0, SINGLE_BUF_BYTES);
        } else {
            LockAudioDevice();
            AudioCallback(this,
                          reinterpret_cast<uint8_t*>(m_axInterleavedScratch.data()),
                          static_cast<int>(SINGLE_BUF_BYTES));
            UnlockAudioDevice();

            // Desentrelazar canal L [0..N-1] y canal R [N..2N-1] fuera del mutex de audio
            int16_t* dstL = targetBuf;
            int16_t* dstR = targetBuf + AX_BUFFER_SAMPLES;
            for (uint32_t i = 0u; i < AX_BUFFER_SAMPLES; ++i) {
                dstL[i] = m_axInterleavedScratch[i * 2u + 0u];
                dstR[i] = m_axInterleavedScratch[i * 2u + 1u];
            }
        }

        DCStoreRange(targetBuf, SINGLE_BUF_BYTES);
        m_axRenderingId.store((renderId + 1u) & 1u, std::memory_order_release);
    }
}
#endif

void AudioManager::BackgroundLoadWorker() {
#if defined(_WIN32) && !defined(__WIIU__) && !defined(__wiiu__)
    if (!m_audioOpened.load(std::memory_order_acquire)) {
        SetCurrentAssetLabel("INICIALIZANDO HARDWARE DE AUDIO...");
        m_actualFreq = 48000;
        m_actualChannels = 2;
        m_actualFormat = 0x8010u; // PCM S16
        AudioEngine3D::Instance().Init();
        if (Platform::OpenWin32AudioDevice(m_actualFreq, m_actualChannels, &AudioManager::HostAudioCallback, this)) {
            m_audioOpened.store(true, std::memory_order_release);
            std::lock_guard<std::mutex> lock(m_stringMutex);
            m_statusString = "AUDIO WIN32 OK (48000Hz 2ch)";
        }
    }
#endif

    // Pre-calcular monótonamente el tamaño exacto total antes de iniciar la lectura
    int64_t clickSize = 0, musicSize = 0;
    ResolveAssetPath("click.wav", clickSize);
    ResolveAssetPath("music.wav", musicSize);
    const size_t safeClickBytes = static_cast<size_t>(std::max<int64_t>(0, clickSize));
    const size_t safeMusicBytes = static_cast<size_t>(std::max<int64_t>(0, musicSize));
    size_t grandTotal = safeClickBytes + safeMusicBytes;
    if (grandTotal == 0) grandTotal = 1;
    m_totalBytesToLoad.store(grandTotal, std::memory_order_relaxed);

    // 1. Cargar CLICK.WAV progresivamente
    std::vector<int16_t> loadedClick;
    if (LoadWAVProgressive("click.wav", loadedClick, 1, "CARGANDO EFECTO SONORO TACTIL (CLICK.WAV)...")) {
        LockAudioDevice();
        m_clickSamples.swap(loadedClick);
        UnlockAudioDevice();
    }

    // 2. [OPT-47] Abrir MUSIC.WAV en modo Streaming Ping-Pong (<= 256 KB RAM) o fallback progresivo
    const bool useStreaming = CVarRegistry::Instance().GetBool("audio.bgm_streaming", true);
    bool bgmReady = false;
    if (useStreaming) {
        SetCurrentAssetLabel("INICIANDO STREAMING BGM PING-PONG (<= 256 KB RAM)...");
        if (OpenBGMStream("music.wav")) {
            m_bytesLoaded.fetch_add(safeMusicBytes, std::memory_order_relaxed);
            bgmReady = true;
        }
    }

    if (!bgmReady) {
        std::vector<int16_t> loadedBGM;
        if (!LoadWAVProgressive("music.wav", loadedBGM, 2, "CARGANDO BANDA SONORA (MUSIC.WAV) EN MEMORIA RAM...")) {
            std::vector<int16_t> fallbackBGM;
            GenerateFallbackBGMSamples(fallbackBGM);
            LockAudioDevice();
            m_bgmSamples.swap(fallbackBGM);
            m_bgmPlayIndex = 0;
            m_bgmPlaying.store(true, std::memory_order_release);
            UnlockAudioDevice();
        } else {
            LockAudioDevice();
            m_bgmSamples.swap(loadedBGM);
            m_bgmPlayIndex = 0;
            m_bgmPlaying.store(true, std::memory_order_release);
            UnlockAudioDevice();
        }
    }

    // Garantizar que bytesLoaded iguale el total planeado al 100%
    m_bytesLoaded.store(m_totalBytesToLoad.load(std::memory_order_relaxed), std::memory_order_relaxed);

    SetCurrentAssetLabel("¡CARGA COMPLETA! INICIANDO MENU PRINCIPAL...");
    m_isLoading = false;
    m_isFinished.store(true, std::memory_order_release);
}

bool AudioManager::Init() {
    StartAsyncInit();
    if (m_loadingThread.joinable()) {
        m_loadingThread.join();
    }
    return m_audioOpened.load(std::memory_order_acquire);
}

void AudioManager::Shutdown() {
    if (m_loadingThread.joinable()) {
        m_loadingThread.join();
    }
#if defined(__WIIU__) || defined(__wiiu__)
    m_axMixThreadRunning.store(false, std::memory_order_release);
    if (m_axMixThread.joinable()) {
        m_axMixThread.join();
    }
#endif
    CloseBGMStream();
    if (m_audioOpened.load(std::memory_order_acquire)) {
        AudioEngine3D::Instance().Shutdown();
        m_audioOpened.store(false, std::memory_order_release);
#if defined(__WIIU__) || defined(__wiiu__)
        s_axInstance.store(nullptr, std::memory_order_release);
        if (m_axCallbackRegistered) {
            AXDeregisterAppFrameCallback(&AudioManager::AXFrameCallbackStatic);
            m_axCallbackRegistered = false;
        }
        for (int ch = 0; ch < 2; ++ch) {
            if (m_axVoices[ch]) {
                AXVoiceBegin(m_axVoices[ch]);
                AXSetVoiceState(m_axVoices[ch], AX_VOICE_STATE_STOPPED);
                AXVoiceEnd(m_axVoices[ch]);
                AXFreeVoice(m_axVoices[ch]);
                m_axVoices[ch] = nullptr;
            }
        }
        m_axVoicesStarted.store(false, std::memory_order_release);

        // Detener el DSP hardware ANTES de liberar el búfer DMA para erradicar Use-After-Free
        AXQuit();

        if (m_axDmaBuffer) {
            {
                void* dmaPtr = m_axDmaBuffer;
                MemoryManager::Instance().FreeAligned(dmaPtr);
            }
            m_axDmaBuffer = nullptr;
        }
        m_axMixBufs[0] = nullptr;
        m_axMixBufs[1] = nullptr;
#elif defined(_WIN32)
        Platform::CloseWin32AudioDevice();
#endif
    }
}

#if defined(__WIIU__) || defined(__wiiu__)
void AudioManager::AXFrameCallbackStatic() {
    AudioManager* self = s_axInstance.load(std::memory_order_acquire);
    if (!self || !self->m_audioOpened.load(std::memory_order_acquire) ||
        !self->m_axVoices[0] || !self->m_axVoices[1] ||
        !self->m_axMixBufs[0] || !self->m_axMixBufs[1]) {
        return;
    }

    AXVoiceOffsets offs[2]{};
    AXGetVoiceOffsets(self->m_axVoices[0], &offs[0]);
    AXGetVoiceOffsets(self->m_axVoices[1], &offs[1]);

    if (!self->m_axVoicesStarted.load(std::memory_order_acquire)) {
        for (int ch = 0; ch < 2; ++ch) {
            AXVoiceBegin(self->m_axVoices[ch]);
            AXSetVoiceState(self->m_axVoices[ch], AX_VOICE_STATE_PLAYING);
            AXVoiceEnd(self->m_axVoices[ch]);
        }
        self->m_axVoicesStarted.store(true, std::memory_order_release);
        return;
    }

    // En Wii U sndcore2, AXSetVoiceOffsets/GetVoiceOffsets reporta desplazamientos relativos a offs.data en MEM2
    const uintptr_t mix0Addr = reinterpret_cast<uintptr_t>(self->m_axMixBufs[0]);
    const uintptr_t mix1Addr = reinterpret_cast<uintptr_t>(self->m_axMixBufs[1]);
    const uint32_t renderingId = self->m_axRenderingId.load(std::memory_order_acquire);

    for (int ch = 0; ch < 2; ++ch) {
        const uintptr_t voiceBase = reinterpret_cast<uintptr_t>(offs[ch].data);
        const uintptr_t chMix0 = mix0Addr + static_cast<size_t>(ch) * AX_BUFFER_SAMPLES * sizeof(int16_t);
        const uintptr_t chMix1 = mix1Addr + static_cast<size_t>(ch) * AX_BUFFER_SAMPLES * sizeof(int16_t);
        const uint32_t start0 = static_cast<uint32_t>((chMix0 - voiceBase) / sizeof(int16_t));
        const uint32_t start1 = static_cast<uint32_t>((chMix1 - voiceBase) / sizeof(int16_t));

        uint32_t chPlayingBuf = 0u;
        if (offs[ch].currentOffset >= start0 && offs[ch].currentOffset < start0 + AX_BUFFER_SAMPLES) {
            chPlayingBuf = 0u;
        } else if (offs[ch].currentOffset >= start1 && offs[ch].currentOffset < start1 + AX_BUFFER_SAMPLES) {
            chPlayingBuf = 1u;
        }

        if (ch == 0) {
            self->m_axPlayingId.store(chPlayingBuf, std::memory_order_release);
        }

        const uint32_t otherBuf = (chPlayingBuf + 1u) & 1u;
        const uint32_t nextBuf = (renderingId != otherBuf) ? otherBuf : chPlayingBuf;

        const uintptr_t curChStart =
            reinterpret_cast<uintptr_t>(self->m_axMixBufs[chPlayingBuf] + static_cast<size_t>(ch) * AX_BUFFER_SAMPLES);
        const uintptr_t curChEnd = curChStart + static_cast<uintptr_t>(AX_BUFFER_SAMPLES - 1u) * sizeof(int16_t);
        const uintptr_t nextChStart =
            reinterpret_cast<uintptr_t>(self->m_axMixBufs[nextBuf] + static_cast<size_t>(ch) * AX_BUFFER_SAMPLES);

        AXSetVoiceEndOffset(self->m_axVoices[ch], static_cast<uint32_t>((curChEnd - voiceBase) / sizeof(int16_t)));
        AXSetVoiceLoopOffset(self->m_axVoices[ch], static_cast<uint32_t>((nextChStart - voiceBase) / sizeof(int16_t)));
    }
}
#endif

void AudioManager::GenerateFallbackSamples() {
    constexpr int FREQ = 48000;
    constexpr int DURATION_MS = 40;
    int numSamples = (FREQ * DURATION_MS) / 1000;
    m_clickSamples.resize(numSamples);

    for (int i = 0; i < numSamples; ++i) {
        float t = static_cast<float>(i) / static_cast<float>(FREQ);
        float decay = 1.0f - (static_cast<float>(i) / static_cast<float>(numSamples));
        float wave = std::sin(2.0f * 3.14159265f * 950.0f * t);
        m_clickSamples[i] = static_cast<int16_t>(wave * decay * 30000.0f);
    }
}

void AudioManager::GenerateFallbackBGMSamples(std::vector<int16_t>& out) {
    constexpr int FREQ = 48000;
    constexpr int DURATION_SEC = 6;
    int numFrames = FREQ * DURATION_SEC;
    out.resize(numFrames * 2); // 2 channels (Stereo)

    for (int i = 0; i < numFrames; ++i) {
        float t = static_cast<float>(i) / static_cast<float>(FREQ);
        float wave1 = std::sin(2.0f * 3.14159265f * 220.0f * t);
        float wave2 = std::sin(2.0f * 3.14159265f * 261.63f * t);
        float wave3 = std::sin(2.0f * 3.14159265f * 329.63f * t);
        float lfo = 0.6f + 0.4f * std::sin(2.0f * 3.14159265f * 0.5f * t);
        float synth = (wave1 * 0.5f + wave2 * 0.3f + wave3 * 0.2f) * lfo;
        int16_t sample = static_cast<int16_t>(synth * 4000.0f);
        out[i * 2]     = sample; // Left
        out[i * 2 + 1] = sample; // Right
    }
}

bool AudioManager::FillStreamHalf(size_t halfIndex) {
    if (halfIndex >= 2) return false;

    std::lock_guard<std::mutex> lock(m_streamFileMutex);
    if (!m_streamFile || m_streamDataBytes == 0) {
        return false;
    }

    int16_t* dst = m_streamBuffers[halfIndex].data();
    const size_t dstStereoFrames = BGM_STREAM_HALF_SAMPLES / 2;
    const size_t bytesPerSrcFrame = static_cast<size_t>(m_streamChannels) * sizeof(int16_t);
    if (bytesPerSrcFrame == 0) return false;

    // Bufer de lectura en pila (4 KB) para 0 heap allocs durante el rellenado
    uint8_t rawChunk[4096];
    const size_t maxRawFramesPerRead = sizeof(rawChunk) / bytesPerSrcFrame;
    size_t outFrame = 0;

    while (outFrame < dstStereoFrames) {
        if (m_streamDataCursor >= m_streamDataBytes) {
            m_streamDataCursor = 0;
            std::fseek(m_streamFile, static_cast<long>(m_streamDataOffset), SEEK_SET);
        }

        const size_t remBytes = m_streamDataBytes - m_streamDataCursor;
        const size_t remSrcFrames = remBytes / bytesPerSrcFrame;
        if (remSrcFrames == 0) {
            m_streamDataCursor = 0;
            std::fseek(m_streamFile, static_cast<long>(m_streamDataOffset), SEEK_SET);
            continue;
        }

        const size_t framesNeeded = dstStereoFrames - outFrame;
        const size_t framesToRead = std::min({framesNeeded, maxRawFramesPerRead, remSrcFrames});
        const size_t bytesToRead = framesToRead * bytesPerSrcFrame;

        const size_t nRead = std::fread(rawChunk, 1, bytesToRead, m_streamFile);
        if (nRead < bytesPerSrcFrame) {
            m_streamDataCursor = 0;
            std::fseek(m_streamFile, static_cast<long>(m_streamDataOffset), SEEK_SET);
            break;
        }

        m_streamDataCursor += nRead;
        const size_t framesGot = nRead / bytesPerSrcFrame;

        if (m_streamChannels == 2) {
            for (size_t f = 0; f < framesGot; ++f) {
                dst[(outFrame + f) * 2 + 0] = static_cast<int16_t>(ReadLE16(rawChunk + f * 4 + 0));
                dst[(outFrame + f) * 2 + 1] = static_cast<int16_t>(ReadLE16(rawChunk + f * 4 + 2));
            }
        } else {
            for (size_t f = 0; f < framesGot; ++f) {
                const int16_t s = static_cast<int16_t>(ReadLE16(rawChunk + f * 2));
                dst[(outFrame + f) * 2 + 0] = s;
                dst[(outFrame + f) * 2 + 1] = s;
            }
        }
        outFrame += framesGot;
    }

    m_streamValidSamples[halfIndex] = outFrame * 2;
    m_streamNeedsRefill[halfIndex].store(false, std::memory_order_release);
    return outFrame > 0;
}

bool AudioManager::OpenBGMStream(const std::string& filepath) {
    CloseBGMStream();

    int64_t fileSize = 0;
    std::string foundPath = ResolveAssetPath(filepath, fileSize);
    if (foundPath.empty() || fileSize < 44) {
        return false;
    }

    FILE* f = std::fopen(foundPath.c_str(), "rb");
    if (!f) return false;

    uint8_t riffHdr[12]{};
    if (std::fread(riffHdr, 1, 12, f) != 12 ||
        std::memcmp(riffHdr, "RIFF", 4) != 0 ||
        std::memcmp(riffHdr + 8, "WAVE", 4) != 0) {
        std::fclose(f);
        return false;
    }

    uint16_t audioFormat = 0;
    uint16_t channels = 0;
    uint32_t sampleRate = 0;
    uint16_t bitsPerSample = 0;
    size_t dataOffset = 0;
    size_t dataBytes = 0;

    while (true) {
        uint8_t chunkHdr[8]{};
        if (std::fread(chunkHdr, 1, 8, f) != 8) break;
        const uint32_t chunkSize = ReadLE32(chunkHdr + 4);

        if (std::memcmp(chunkHdr, "fmt ", 4) == 0 && chunkSize >= 16u) {
            uint8_t fmtBuf[16]{};
            if (std::fread(fmtBuf, 1, 16, f) != 16) break;
            audioFormat = ReadLE16(fmtBuf + 0);
            channels = ReadLE16(fmtBuf + 2);
            sampleRate = ReadLE32(fmtBuf + 4);
            bitsPerSample = ReadLE16(fmtBuf + 14);
            if (chunkSize > 16u) {
                if (chunkSize >= 0x7FFFFFFEu) break;
                const uint32_t paddedFmt = (chunkSize + 1u) & ~1u;
                const uint32_t remFmt = paddedFmt - 16u;
                if (static_cast<int64_t>(remFmt) > fileSize) break;
                if (std::fseek(f, static_cast<long>(remFmt), SEEK_CUR) != 0) break;
            }
        } else if (std::memcmp(chunkHdr, "data", 4) == 0) {
            long pos = std::ftell(f);
            if (pos > 0) {
                dataOffset = static_cast<size_t>(pos);
                const size_t maxAvail = (static_cast<size_t>(fileSize) > dataOffset)
                                        ? (static_cast<size_t>(fileSize) - dataOffset) : 0;
                dataBytes = (chunkSize == 0xFFFFFFFFu || chunkSize > maxAvail) ? maxAvail : chunkSize;
            }
            break;
        } else {
            if (chunkSize >= 0x7FFFFFFEu) break;
            const uint32_t skip = (chunkSize + 1u) & ~1u;
            if (static_cast<int64_t>(skip) > fileSize) break;
            if (std::fseek(f, static_cast<long>(skip), SEEK_CUR) != 0) break;
        }
    }

    // Streaming directo solo para PCM16 mono/estereo a frecuencia coincidente (evita distorsion de pitch)
    if (audioFormat != 1 || bitsPerSample != 16 || (channels != 1 && channels != 2) ||
        static_cast<int>(sampleRate) != m_actualFreq || dataOffset == 0 || dataBytes < 64) {
        std::fclose(f);
        return false;
    }

    {
        std::lock_guard<std::mutex> lock(m_streamFileMutex);
        m_streamFile = f;
        m_streamDataOffset = dataOffset;
        m_streamDataBytes = dataBytes;
        m_streamDataCursor = 0;
        m_streamChannels = channels;
        m_streamSampleRate = sampleRate;
        std::fseek(m_streamFile, static_cast<long>(m_streamDataOffset), SEEK_SET);
    }

    if (!FillStreamHalf(0) || !FillStreamHalf(1)) {
        CloseBGMStream();
        return false;
    }

    std::vector<int16_t> discardedBgm;
    LockAudioDevice();
    m_streamActiveHalf = 0;
    m_streamReadPos = 0;
    m_streamNeedsRefill[0].store(false, std::memory_order_release);
    m_streamNeedsRefill[1].store(false, std::memory_order_release);
    m_streamActive.store(true, std::memory_order_release);
    m_bgmPlaying.store(true, std::memory_order_release);
    m_bgmSamples.swap(discardedBgm); // Liberar heap fuera de LockAudioDevice (OPT-437)
    UnlockAudioDevice();
    discardedBgm.clear();
    discardedBgm.shrink_to_fit();

    {
        std::lock_guard<std::mutex> lock(m_stringMutex);
        m_statusString += " | BGM STREAMING 256KB OK";
    }
    return true;
}

void AudioManager::CloseBGMStream() {
    // 1. Detener consumo del callback de audio bajo exclusión mutua del dispositivo de audio.
    LockAudioDevice();
    m_streamActive.store(false, std::memory_order_release);
    m_streamActiveHalf = 0;
    m_streamReadPos = 0;
    UnlockAudioDevice();

    // 2. Esperar cooperativamente a que cualquier job de rellenado en Core 2 termine antes de cerrar el archivo.
    if (m_streamRefillCounter.load(std::memory_order_acquire) > 0u && JobSystem::Instance().IsInitialized()) {
        JobSystem::Instance().Wait(&m_streamRefillCounter);
    }
    m_streamRefillInFlight.store(false, std::memory_order_release);

    {
        std::lock_guard<std::mutex> lock(m_streamFileMutex);
        if (m_streamFile) {
            std::fclose(m_streamFile);
            m_streamFile = nullptr;
        }
        m_streamDataOffset = 0;
        m_streamDataBytes = 0;
        m_streamDataCursor = 0;
    }

    LockAudioDevice();
    m_streamValidSamples[0] = 0;
    m_streamValidSamples[1] = 0;
    m_streamNeedsRefill[0].store(false, std::memory_order_release);
    m_streamNeedsRefill[1].store(false, std::memory_order_release);
    UnlockAudioDevice();
}

void AudioManager::UpdateStreaming() {
    if (!m_streamActive.load(std::memory_order_acquire)) return;

    const bool need0 = m_streamNeedsRefill[0].load(std::memory_order_acquire);
    const bool need1 = m_streamNeedsRefill[1].load(std::memory_order_acquire);
    if (!need0 && !need1) return;

    if (m_streamRefillCounter.load(std::memory_order_acquire) != 0u) {
        return;
    }

    bool expected = false;
    if (!m_streamRefillInFlight.compare_exchange_strong(expected, true, std::memory_order_acq_rel)) {
        return;
    }

    auto refillWork = [](void* userdata) {
        auto* self = static_cast<AudioManager*>(userdata);
        if (self && self->m_streamActive.load(std::memory_order_acquire)) {
            if (self->m_streamNeedsRefill[0].load(std::memory_order_acquire)) {
                self->FillStreamHalf(0);
            }
            if (self->m_streamNeedsRefill[1].load(std::memory_order_acquire)) {
                self->FillStreamHalf(1);
            }
        }
        if (self) {
            self->m_streamRefillInFlight.store(false, std::memory_order_release);
        }
    };

    if (JobSystem::Instance().IsInitialized()) {
        JobSystem::Instance().Dispatch(refillWork, this, &m_streamRefillCounter);
    } else {
        refillWork(this);
    }
}

bool AudioManager::LoadBGM(const std::string& filepath) {
    CloseBGMStream();
    std::vector<int16_t> loadedBGM;
    if (LoadWAVHelper(filepath, loadedBGM, 2)) {
        LockAudioDevice();
        m_bgmSamples.swap(loadedBGM);
        m_bgmPlayIndex = 0;
        m_bgmPlaying.store(true, std::memory_order_release);
        UnlockAudioDevice();
        return true;
    }

    // Fallback: Sintetizador estéreo generado en hilo de trabajo sin lock
    std::vector<int16_t> fallbackBGM;
    GenerateFallbackBGMSamples(fallbackBGM);
    LockAudioDevice();
    m_bgmSamples.swap(fallbackBGM);
    m_bgmPlayIndex = 0;
    m_bgmPlaying.store(true, std::memory_order_release);
    UnlockAudioDevice();
    {
        std::lock_guard<std::mutex> lock(m_stringMutex);
        m_statusString += " | SYNTH FALLBACK USED";
    }
    return false;
}

void AudioManager::PlayBGM() {
    m_bgmPlaying.store(true, std::memory_order_release);
}

void AudioManager::RestartBGM() {
    if (m_streamActive.load(std::memory_order_acquire)) {
        LockAudioDevice();
        m_streamActive.store(false, std::memory_order_release);
        UnlockAudioDevice();

        if (m_streamRefillCounter.load(std::memory_order_acquire) > 0u && JobSystem::Instance().IsInitialized()) {
            JobSystem::Instance().Wait(&m_streamRefillCounter);
        }
        m_streamRefillInFlight.store(false, std::memory_order_release);

        {
            std::lock_guard<std::mutex> lock(m_streamFileMutex);
            if (m_streamFile) {
                m_streamDataCursor = 0;
                std::fseek(m_streamFile, static_cast<long>(m_streamDataOffset), SEEK_SET);
            }
        }

        const bool ok0 = FillStreamHalf(0);
        const bool ok1 = ok0 && FillStreamHalf(1);

        LockAudioDevice();
        m_streamActiveHalf = 0;
        m_streamReadPos = 0;
        m_streamNeedsRefill[0].store(false, std::memory_order_release);
        m_streamNeedsRefill[1].store(false, std::memory_order_release);
        m_streamActive.store(ok1, std::memory_order_release);
        m_bgmPlaying.store(ok1, std::memory_order_release);
        UnlockAudioDevice();
    } else {
        LockAudioDevice();
        m_bgmPlayIndex = 0;
        m_bgmPlaying.store(true, std::memory_order_release);
        UnlockAudioDevice();
    }
}

void AudioManager::StopBGM() {
    LockAudioDevice();
    m_bgmPlaying = false;
    m_bgmPlayIndex = 0;
    UnlockAudioDevice();
}

void AudioManager::SetBGMVolume(float volume) {
    m_bgmVolume.store(std::clamp(volume, 0.0f, 1.0f), std::memory_order_release);
}

void AudioManager::SetMasterVolume(float volume) {
    m_masterVolume.store(std::clamp(volume, 0.0f, 1.0f), std::memory_order_release);
}

void AudioManager::SetMusicEnabled(bool enabled) {
    m_musicEnabled.store(enabled, std::memory_order_release);
}

void AudioManager::ToggleMusic() {
    SetMusicEnabled(!m_musicEnabled.load(std::memory_order_relaxed));
}

void AudioManager::SetPaused(bool paused) {
    m_isPaused.store(paused, std::memory_order_release);
}

void AudioManager::SetSFXEnabled(bool enabled) noexcept {
    m_sfxEnabled.store(enabled, std::memory_order_release);
    float vol = m_sfxVolume.load(std::memory_order_acquire);
    AudioEngine3D::Instance().SetBusVolume(AudioBus::SFX, enabled ? vol : 0.0f);
}

void AudioManager::SetSFXVolume(float volume) noexcept {
    float clamped = std::clamp(volume, 0.0f, 1.0f);
    m_sfxVolume.store(clamped, std::memory_order_release);
    bool enabled = m_sfxEnabled.load(std::memory_order_acquire);
    AudioEngine3D::Instance().SetBusVolume(AudioBus::SFX, enabled ? clamped : 0.0f);
}

void AudioManager::PlayClick() {
    if (!m_sfxEnabled.load(std::memory_order_relaxed) || m_sfxVolume.load(std::memory_order_relaxed) <= 0.001f) {
        return;
    }
    LockAudioDevice();
    m_sfxPlayIndex = 0;
    m_sfxPlaying = true;
    UnlockAudioDevice();
}

void AudioManager::HostAudioCallback(void* userdata, uint8_t* stream, int len) {
    if (!stream || len <= 0) return;
    auto* self = static_cast<AudioManager*>(userdata);
    if (!self) {
        std::memset(stream, 0, static_cast<size_t>(len));
        return;
    }
    self->LockAudioDevice();
    AudioCallback(userdata, stream, len);
    self->UnlockAudioDevice();
}

void AudioManager::AudioCallback(void* userdata, uint8_t* stream, int len) {
    std::memset(stream, 0, len);
    auto* self = static_cast<AudioManager*>(userdata);
    if (!self) return;

    if (self->m_isPaused.load(std::memory_order_acquire)) {
        // En pausa: congelar avance de cursores de BGM y AudioEngine3D (silencio de escena).
        // Si hay un efecto SFX de UI (click de botón del modal de pausa), permitir su emisión táctil si está habilitado:
        if (self->m_sfxEnabled && self->m_sfxPlaying && !self->m_clickSamples.empty()) {
            int16_t* out = reinterpret_cast<int16_t*>(stream);
            int totalSamples = len / static_cast<int>(sizeof(int16_t));
            int numFrames = totalSamples / 2;
            const float masterVol = self->m_masterVolume.load(std::memory_order_relaxed) * self->m_sfxVolume.load(std::memory_order_relaxed);
            for (int i = 0; i < numFrames; ++i) {
                if (self->m_sfxPlaying && self->m_sfxPlayIndex < self->m_clickSamples.size()) {
                    int32_t sfxVal = static_cast<int32_t>(self->m_clickSamples[self->m_sfxPlayIndex++]);
                    sfxVal = static_cast<int32_t>(sfxVal * masterVol);
                    int16_t clampedVal = static_cast<int16_t>(std::clamp(sfxVal, -32768, 32767));
                    out[i * 2]     = clampedVal;
                    out[i * 2 + 1] = clampedVal;
                } else {
                    self->m_sfxPlaying = false;
                    self->m_sfxPlayIndex = 0;
                    break;
                }
            }
        }
        return;
    }

    int16_t* out = reinterpret_cast<int16_t*>(stream);
    int totalSamples = len / static_cast<int>(sizeof(int16_t));
    int numFrames = totalSamples / 2; // Stereo output

    const float masterVol = self->m_masterVolume.load(std::memory_order_relaxed);
    const bool musicOn = self->m_musicEnabled.load(std::memory_order_relaxed) && self->m_bgmPlaying.load(std::memory_order_relaxed);
    const bool sfxOn = self->m_sfxEnabled.load(std::memory_order_relaxed) && self->m_sfxPlaying;
    const float sfxVol = self->m_sfxVolume.load(std::memory_order_relaxed);
    const float bgmVol = self->m_bgmVolume.load(std::memory_order_relaxed);

    for (int i = 0; i < numFrames; ++i) {
        int32_t mixL = 0;
        int32_t mixR = 0;

        // 1. Música de Fondo (Stereo BGM: [OPT-47] Ping-Pong Streaming <= 256 KB o fallback RAM)
        if (musicOn) {
            if (self->m_streamActive.load(std::memory_order_acquire)) {
                const size_t activeHalf = self->m_streamActiveHalf;
                // [PPC750CL Weak Memory Model]: Acquire de m_streamNeedsRefill ANTES de leer m_streamValidSamples/Buffers.
                if (!self->m_streamNeedsRefill[activeHalf].load(std::memory_order_acquire)) {
                    const size_t validCount = self->m_streamValidSamples[activeHalf];
                    if (self->m_streamReadPos + 1 < validCount) {
                        const auto& buf = self->m_streamBuffers[activeHalf];
                        mixL += static_cast<int32_t>(buf[self->m_streamReadPos]     * bgmVol);
                        mixR += static_cast<int32_t>(buf[self->m_streamReadPos + 1] * bgmVol);
                        self->m_streamReadPos += 2;
                        if (self->m_streamReadPos >= validCount) {
                            self->m_streamNeedsRefill[activeHalf].store(true, std::memory_order_release);
                            self->m_streamActiveHalf = 1u - activeHalf;
                            self->m_streamReadPos = 0;
                        }
                    } else {
                        // Evitar stall silencioso si validCount == 0 o cursor al limite
                        self->m_streamNeedsRefill[activeHalf].store(true, std::memory_order_release);
                        self->m_streamActiveHalf = 1u - activeHalf;
                        self->m_streamReadPos = 0;
                    }
                }
            } else if (!self->m_bgmSamples.empty()) {
                size_t idx = self->m_bgmPlayIndex;
                if (idx + 1 < self->m_bgmSamples.size()) {
                    mixL += static_cast<int32_t>(self->m_bgmSamples[idx]     * bgmVol);
                    mixR += static_cast<int32_t>(self->m_bgmSamples[idx + 1] * bgmVol);
                }
                self->m_bgmPlayIndex += 2;
                if (self->m_bgmPlayIndex >= self->m_bgmSamples.size()) {
                    self->m_bgmPlayIndex = 0; // Seamless loop!
                }
            }
        }

        // 2. Efecto de Sonido Click (Mono SFX duplicado a L + R con sfxVol)
        if (sfxOn && !self->m_clickSamples.empty()) {
            if (self->m_sfxPlayIndex < self->m_clickSamples.size()) {
                int32_t sfxVal = static_cast<int32_t>(self->m_clickSamples[self->m_sfxPlayIndex++] * sfxVol);
                mixL += sfxVal;
                mixR += sfxVal;
            } else {
                self->m_sfxPlaying = false;
                self->m_sfxPlayIndex = 0;
            }
        }

        // 3. Aplicar Volumen Maestro General a toda la salida de audio
        mixL = static_cast<int32_t>(mixL * masterVol);
        mixR = static_cast<int32_t>(mixR * masterVol);

        out[i * 2]     = static_cast<int16_t>(std::clamp(mixL, -32768, 32767));
        out[i * 2 + 1] = static_cast<int16_t>(std::clamp(mixR, -32768, 32767));
    }

    // 4. Mezclar canales de audio espacial 3D del Voice Pool (AudioEngine3D)
    AudioEngine3D::Instance().MixAudio(out, numFrames, masterVol);
}

} // namespace Neleo

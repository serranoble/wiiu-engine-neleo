#pragma once

#if !defined(__WIIU__) && !defined(__wiiu__)
#include <SDL2/SDL.h>
#else
#include <coreinit/fastmutex.h>
#include <coreinit/cache.h>
#include <coreinit/memorymap.h>
#include <coreinit/thread.h>
#include <coreinit/time.h>
#include <sndcore2/core.h>
#include <sndcore2/voice.h>
#include <sndcore2/device.h>
#endif
#include <array>
#include <cstdio>
#include <vector>
#include <cstdint>
#include <string>
#include <atomic>
#include <thread>
#include <mutex>

namespace WiiU {

class AudioManager {
public:
    static AudioManager& Instance() {
        static AudioManager instance;
        return instance;
    }

    AudioManager(const AudioManager&) = delete;
    AudioManager& operator=(const AudioManager&) = delete;
    AudioManager(AudioManager&&) = delete;
    AudioManager& operator=(AudioManager&&) = delete;

    bool Init();
    void StartAsyncInit();
    void Shutdown();

    // SFX (Efectos de sonido de click y buses 3D)
    void PlayClick();
    void SetSFXEnabled(bool enabled) noexcept;
    [[nodiscard]] bool IsSFXEnabled() const noexcept { return m_sfxEnabled.load(std::memory_order_relaxed); }
    void SetSFXVolume(float volume) noexcept;
    [[nodiscard]] float GetSFXVolume() const noexcept { return m_sfxVolume.load(std::memory_order_relaxed); }

    // BGM (Música de fondo en bucle)
    bool LoadBGM(const std::string& filepath);
    /**
     * @brief [OPT-47] Abre un archivo WAV para streaming ping-pong con huella <= 256 KB en RAM.
     */
    bool OpenBGMStream(const std::string& filepath);
    void CloseBGMStream();
    /**
     * @brief [OPT-47] Rellena la mitad agotada del bufer doble ping-pong sin bloquear el hilo de audio.
     */
    void UpdateStreaming();
    [[nodiscard]] bool IsBGMStreamingActive() const noexcept { return m_streamActive.load(std::memory_order_acquire); }
    void PlayBGM();
    void RestartBGM();
    void StopBGM();
    void SetBGMVolume(float volume);
    [[nodiscard]] bool IsBGMPlaying() const noexcept { return m_bgmPlaying.load(std::memory_order_relaxed); }

    // Control de Volumen Maestro y Estado de Música
    void SetMasterVolume(float volume);
    [[nodiscard]] float GetMasterVolume() const noexcept { return m_masterVolume.load(std::memory_order_relaxed); }
    void SetMusicEnabled(bool enabled);
    [[nodiscard]] bool IsMusicEnabled() const noexcept { return m_musicEnabled.load(std::memory_order_relaxed); }
    void ToggleMusic();

    // Control de Pausa Global (Silenciamiento y Congelación de Cursores BGM / 3D)
    void SetPaused(bool paused);
    [[nodiscard]] bool IsPaused() const noexcept { return m_isPaused.load(std::memory_order_relaxed); }

    // Bloqueo de dispositivo/mutex de mezcla para sincronización con AudioEngine3D
    void LockAudioDevice() noexcept;
    void UnlockAudioDevice() noexcept;

    // Progreso de Carga en Tiempo Real (Thread-Safe)
    float GetProgress() const;
    size_t GetBytesLoaded() const { return m_bytesLoaded.load(std::memory_order_relaxed); }
    size_t GetTotalBytes() const { return m_totalBytesToLoad.load(std::memory_order_relaxed); }
    bool IsLoadingFinished() const { return m_isFinished.load(std::memory_order_acquire); }
    
    std::string GetCurrentAssetLoading() const;
    void CopyCurrentAssetLabel(char* outBuf, size_t maxLen) const noexcept;
    void SetCurrentAssetLabel(const std::string& label);
    std::string GetStatusString() const;

private:
    AudioManager();
    ~AudioManager();

    static void AudioCallback(void* userdata, uint8_t* stream, int len);
    void GenerateFallbackSamples();
    void GenerateFallbackBGMSamples(std::vector<int16_t>& out);

    void BackgroundLoadWorker();
    bool FillStreamHalf(size_t halfIndex);
    bool LoadWAVProgressive(const std::string& filepath, std::vector<int16_t>& outSamples, int targetChannels, const std::string& assetLabel);
    bool LoadWAVHelper(const std::string& filepath, std::vector<int16_t>& outSamples, int targetChannels);
    std::string ResolveAssetPath(const std::string& filepath, int64_t& outFileSize) const;

    // SFX Channel
    std::vector<int16_t> m_clickSamples;
    size_t m_sfxPlayIndex{0};
    bool m_sfxPlaying{false};
    std::atomic<bool> m_sfxEnabled{true};
    std::atomic<float> m_sfxVolume{1.0f};

    // BGM Channel (Fallback en memoria + [OPT-47] Double-Buffered Ping-Pong Streaming <= 256 KB)
    static constexpr size_t BGM_STREAM_HALF_SAMPLES = 65536; // 65536 int16 = 128 KB por mitad (256 KB total)
    std::vector<int16_t> m_bgmSamples;
    size_t m_bgmPlayIndex{0};
    std::atomic<bool> m_bgmPlaying{false};
    std::atomic<float> m_bgmVolume{0.45f};

    std::array<std::array<int16_t, BGM_STREAM_HALF_SAMPLES>, 2> m_streamBuffers{};
    std::array<size_t, 2> m_streamValidSamples{0, 0};
    std::array<std::atomic<bool>, 2> m_streamNeedsRefill{false, false};
    size_t m_streamActiveHalf{0};
    size_t m_streamReadPos{0};
    std::atomic<bool> m_streamActive{false};
    std::atomic<bool> m_streamRefillInFlight{false};
    std::atomic<uint32_t> m_streamRefillCounter{0};
    FILE* m_streamFile{nullptr};
    size_t m_streamDataOffset{0};
    size_t m_streamDataBytes{0};
    size_t m_streamDataCursor{0};
    uint16_t m_streamChannels{2};
    uint32_t m_streamSampleRate{48000};
    std::mutex m_streamFileMutex;

    // Master Volume & Music Enable
    std::atomic<float> m_masterVolume{1.0f};
    std::atomic<bool> m_musicEnabled{true};
    std::atomic<bool> m_isPaused{false};

    std::atomic<bool> m_audioOpened{false};
    int m_actualFreq{48000};
    int m_actualChannels{2};
    uint16_t m_actualFormat{0x9010};
    std::string m_statusString{"NOT INITIALIZED"};

#if defined(__WIIU__) || defined(__wiiu__)
    static constexpr uint32_t AX_BUFFER_SAMPLES = 2048u;
    bool InitNativeAXHardware();
    void AudioMixThreadWorker();
    static void AXFrameCallbackStatic();

    OSFastMutex m_deviceMutex{};
    int16_t* m_axDmaBuffer{nullptr};
    int16_t* m_axMixBufs[2]{nullptr, nullptr};
    std::array<int16_t, AX_BUFFER_SAMPLES * 2u> m_axInterleavedScratch{};
    AXVoice* m_axVoices[2]{nullptr, nullptr};
    std::atomic<uint32_t> m_axPlayingId{0u};
    std::atomic<uint32_t> m_axRenderingId{1u};
    std::atomic<bool> m_axVoicesStarted{false};
    std::atomic<bool> m_axMixThreadRunning{false};
    std::thread m_axMixThread;
    bool m_axCallbackRegistered{false};
#endif

    // Proteccion de Hilos para Cadenas de Texto
    mutable std::mutex m_stringMutex;
    std::string m_currentAssetLabel{"INICIALIZANDO..."};

    // Progressive Async Loading State
    std::thread m_loadingThread;
    std::atomic<size_t> m_bytesLoaded{0};
    std::atomic<size_t> m_totalBytesToLoad{1};
    std::atomic<bool> m_isLoading{false};
    std::atomic<bool> m_isFinished{false};
};

} // namespace WiiU

#pragma once

#include <SDL2/SDL.h>
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

    bool Init();
    void StartAsyncInit();
    void Shutdown();

    // SFX (Efectos de sonido de click)
    void PlayClick();

    // BGM (Música de fondo en bucle)
    bool LoadBGM(const std::string& filepath);
    void PlayBGM();
    void RestartBGM();
    void StopBGM();
    void SetBGMVolume(float volume);
    [[nodiscard]] bool IsBGMPlaying() const { return m_bgmPlaying; }

    // Control de Volumen Maestro y Estado de Música
    void SetMasterVolume(float volume);
    [[nodiscard]] float GetMasterVolume() const { return m_masterVolume; }
    void SetMusicEnabled(bool enabled);
    [[nodiscard]] bool IsMusicEnabled() const { return m_musicEnabled; }
    void ToggleMusic();

    // Control de Pausa Global (Silenciamiento y Congelación de Cursores BGM / 3D)
    void SetPaused(bool paused);
    [[nodiscard]] bool IsPaused() const noexcept { return m_isPaused.load(std::memory_order_relaxed); }

    // Progreso de Carga en Tiempo Real (Thread-Safe)
    float GetProgress() const;
    size_t GetBytesLoaded() const { return m_bytesLoaded.load(std::memory_order_relaxed); }
    size_t GetTotalBytes() const { return m_totalBytesToLoad.load(std::memory_order_relaxed); }
    bool IsLoadingFinished() const { return m_isFinished.load(std::memory_order_acquire); }
    
    std::string GetCurrentAssetLoading() const;
    void SetCurrentAssetLabel(const std::string& label);
    std::string GetStatusString() const;

private:
    AudioManager() = default;
    ~AudioManager();

    static void AudioCallback(void* userdata, Uint8* stream, int len);
    void GenerateFallbackSamples();
    void GenerateFallbackBGMSamples();

    void BackgroundLoadWorker();
    bool LoadWAVProgressive(const std::string& filepath, std::vector<int16_t>& outSamples, int targetChannels, const std::string& assetLabel);
    bool LoadWAVHelper(const std::string& filepath, std::vector<int16_t>& outSamples, int targetChannels);
    std::string ResolveAssetPath(const std::string& filepath, Sint64& outFileSize) const;

    // SFX Channel
    std::vector<int16_t> m_clickSamples;
    size_t m_sfxPlayIndex{0};
    bool m_sfxPlaying{false};

    // BGM Channel
    std::vector<int16_t> m_bgmSamples;
    size_t m_bgmPlayIndex{0};
    bool m_bgmPlaying{false};
    float m_bgmVolume{0.45f};

    // Master Volume & Music Enable
    float m_masterVolume{1.0f};
    bool m_musicEnabled{true};
    std::atomic<bool> m_isPaused{false};

    bool m_audioOpened{false};
    int m_actualFreq{48000};
    int m_actualChannels{2};
    Uint16 m_actualFormat{AUDIO_S16SYS};
    std::string m_statusString{"NOT INITIALIZED"};

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

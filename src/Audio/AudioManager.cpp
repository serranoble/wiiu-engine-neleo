#include "Audio/AudioManager.hpp"
#include "Audio/AudioEngine3D.hpp"
#include "Core/AssetManager.hpp"
#include <cstring>
#include <cmath>
#include <algorithm>
#include <iostream>
#include <chrono>

namespace WiiU {

AudioManager::~AudioManager() {
    if (m_loadingThread.joinable()) {
        m_loadingThread.join();
    }
    Shutdown();
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

void AudioManager::SetCurrentAssetLabel(const std::string& label) {
    std::lock_guard<std::mutex> lock(m_stringMutex);
    m_currentAssetLabel = label;
}

std::string AudioManager::GetStatusString() const {
    std::lock_guard<std::mutex> lock(m_stringMutex);
    return m_statusString;
}

std::string AudioManager::ResolveAssetPath(const std::string& filepath, Sint64& outFileSize) const {
    int64_t sz = 0;
    std::string path = AssetManager::ResolvePath(filepath, &sz);
    outFileSize = static_cast<Sint64>(sz);
    return path;
}

bool AudioManager::LoadWAVProgressive(const std::string& filepath, std::vector<int16_t>& outSamples, int targetChannels, const std::string& assetLabel) {
    SetCurrentAssetLabel(assetLabel);

    Sint64 fileSize = 0;
    std::string foundPath = ResolveAssetPath(filepath, fileSize);
    if (foundPath.empty() || fileSize < 44) {
        return false;
    }

    SDL_RWops* rw = SDL_RWFromFile(foundPath.c_str(), "rb");
    if (!rw) {
        m_bytesLoaded.fetch_add(static_cast<size_t>(fileSize), std::memory_order_relaxed);
        return false;
    }

    std::vector<uint8_t> fileBuffer(static_cast<size_t>(fileSize));
    size_t totalRead = 0;
    constexpr size_t CHUNK_SIZE = 131072; // Chunk de 128 KB para streaming suave
    while (totalRead < static_cast<size_t>(fileSize)) {
        size_t toRead = std::min(CHUNK_SIZE, static_cast<size_t>(fileSize) - totalRead);
        size_t nRead = SDL_RWread(rw, fileBuffer.data() + totalRead, 1, toRead);
        if (nRead == 0) break;
        totalRead += nRead;
        m_bytesLoaded.fetch_add(nRead, std::memory_order_relaxed);
        std::this_thread::yield();
    }
    SDL_RWclose(rw);

    if (totalRead != static_cast<size_t>(fileSize)) {
        m_bytesLoaded.fetch_add(static_cast<size_t>(fileSize) - totalRead, std::memory_order_relaxed);
        return false;
    }

    // Auto-corregir encabezado RIFF/data si contiene 0xFFFFFFFF (Streaming WAV)
    if (fileBuffer.size() >= 12 && 
        std::memcmp(fileBuffer.data(), "RIFF", 4) == 0 && 
        std::memcmp(fileBuffer.data() + 8, "WAVE", 4) == 0) {

        uint32_t riffSize = 0;
        std::memcpy(&riffSize, fileBuffer.data() + 4, 4);
        if (riffSize == 0xFFFFFFFF || riffSize > fileBuffer.size() - 8) {
            uint32_t fixedRiff = static_cast<uint32_t>(fileBuffer.size() - 8);
            std::memcpy(fileBuffer.data() + 4, &fixedRiff, 4);
        }

        size_t pos = 12;
        while (pos + 8 <= fileBuffer.size()) {
            char chunkId[4];
            std::memcpy(chunkId, fileBuffer.data() + pos, 4);
            uint32_t chunkSize = 0;
            std::memcpy(&chunkSize, fileBuffer.data() + pos + 4, 4);

            if (std::memcmp(chunkId, "data", 4) == 0) {
                size_t dataStart = pos + 8;
                if (chunkSize == 0xFFFFFFFF || chunkSize > fileBuffer.size() - dataStart) {
                    uint32_t fixedDataSize = static_cast<uint32_t>(fileBuffer.size() - dataStart);
                    std::memcpy(fileBuffer.data() + pos + 4, &fixedDataSize, 4);
                }
                break;
            } else {
                uint32_t stepSize = (chunkSize == 0xFFFFFFFF) ? 0 : ((chunkSize + 1) & ~1u);
                pos += 8 + stepSize;
            }
        }
    }

    SDL_RWops* memRw = SDL_RWFromMem(fileBuffer.data(), static_cast<int>(fileBuffer.size()));
    if (!memRw) return false;

    SDL_AudioSpec wavSpec;
    Uint8* wavBuf = nullptr;
    Uint32 wavLen = 0;

    if (SDL_LoadWAV_RW(memRw, 1, &wavSpec, &wavBuf, &wavLen) == nullptr) {
        return false;
    }

    // Liberar inmediatamente el búfer de archivo (24 MB) para liberar heap en MEM2 antes de convertir
    fileBuffer.clear();
    fileBuffer.shrink_to_fit();

    SDL_AudioCVT cvt;
    int buildRes = SDL_BuildAudioCVT(&cvt, 
                                     wavSpec.format, wavSpec.channels, wavSpec.freq,
                                     m_actualFormat, targetChannels, m_actualFreq);
    if (buildRes < 0) {
        SDL_FreeWAV(wavBuf);
        return false;
    }

    cvt.len = wavLen;
    int bufMult = cvt.len_mult > 0 ? cvt.len_mult : 1;
    cvt.buf = static_cast<Uint8*>(SDL_malloc(wavLen * bufMult));
    if (!cvt.buf) {
        SDL_FreeWAV(wavBuf);
        return false;
    }

    std::memcpy(cvt.buf, wavBuf, wavLen);

    if (cvt.needed) {
        if (SDL_ConvertAudio(&cvt) < 0) {
            SDL_free(cvt.buf);
            SDL_FreeWAV(wavBuf);
            return false;
        }
    } else {
        cvt.len_cvt = cvt.len;
    }

    int totalInt16 = cvt.len_cvt / static_cast<int>(sizeof(int16_t));
    
    // Asignar buffer en el heap del hilo de carga FUERA de la sección crítica de audio
    std::vector<int16_t> tempBuffer(totalInt16);
    std::memcpy(tempBuffer.data(), cvt.buf, cvt.len_cvt);

    SDL_free(cvt.buf);
    SDL_FreeWAV(wavBuf);

    // Swap rápido de punteros bajo Lock sin allocations en heap
    SDL_LockAudio();
    outSamples = std::move(tempBuffer);
    SDL_UnlockAudio();

    {
        std::lock_guard<std::mutex> lock(m_stringMutex);
        m_statusString += " | LOADED " + foundPath + " (" + std::to_string(totalInt16) + " samples)";
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

    m_loadingThread = std::thread(&AudioManager::BackgroundLoadWorker, this);
}

void AudioManager::BackgroundLoadWorker() {
    GenerateFallbackSamples();

    SetCurrentAssetLabel("INICIALIZANDO HARDWARE DE AUDIO ESPRESSO...");

    SDL_AudioSpec desiredSpec;
    std::memset(&desiredSpec, 0, sizeof(desiredSpec));
    desiredSpec.freq = 48000;
    desiredSpec.format = AUDIO_S16SYS;
    desiredSpec.channels = 2; // Stereo
    desiredSpec.samples = 2048;
    desiredSpec.callback = AudioCallback;
    desiredSpec.userdata = this;

    SDL_AudioSpec obtainedSpec;
    std::memset(&obtainedSpec, 0, sizeof(obtainedSpec));

    if (SDL_OpenAudio(&desiredSpec, &obtainedSpec) == 0) {
        m_audioOpened = true;
        m_actualFreq = obtainedSpec.freq > 0 ? obtainedSpec.freq : 48000;
        m_actualChannels = obtainedSpec.channels > 0 ? obtainedSpec.channels : 2;
        m_actualFormat = obtainedSpec.format != 0 ? obtainedSpec.format : AUDIO_S16SYS;
        
        std::lock_guard<std::mutex> lock(m_stringMutex);
        m_statusString = "AUDIO OK (" + std::to_string(m_actualFreq) + "Hz " + std::to_string(m_actualChannels) + "ch)";
        AudioEngine3D::Instance().Init();
    }

    // Pre-calcular monótonamente el tamaño exacto total antes de iniciar la lectura
    Sint64 clickSize = 0, musicSize = 0;
    ResolveAssetPath("click.wav", clickSize);
    ResolveAssetPath("music.wav", musicSize);
    constexpr size_t SHADER_SIM_BYTES = 1024 * 1024; // 1 MB simulación sombreadores/tablas
    size_t grandTotal = static_cast<size_t>(clickSize + musicSize) + SHADER_SIM_BYTES;
    if (grandTotal == 0) grandTotal = 1;
    m_totalBytesToLoad.store(grandTotal, std::memory_order_relaxed);

    // 1. Cargar CLICK.WAV progresivamente
    std::vector<int16_t> loadedClick;
    if (LoadWAVProgressive("click.wav", loadedClick, 1, "CARGANDO EFECTO SONORO TACTIL (CLICK.WAV)...")) {
        SDL_LockAudio();
        m_clickSamples = std::move(loadedClick);
        SDL_UnlockAudio();
    }

    // 2. Cargar MUSIC.WAV progresivamente
    std::vector<int16_t> loadedBGM;
    if (!LoadWAVProgressive("music.wav", loadedBGM, 2, "CARGANDO BANDA SONORA (MUSIC.WAV) EN MEMORIA RAM...")) {
        SDL_LockAudio();
        GenerateFallbackBGMSamples();
        m_bgmPlayIndex = 0;
        m_bgmPlaying = true;
        SDL_UnlockAudio();
    } else {
        SDL_LockAudio();
        m_bgmSamples = std::move(loadedBGM);
        m_bgmPlayIndex = 0;
        m_bgmPlaying = true;
        SDL_UnlockAudio();
    }

    // 3. Simulación de pre-compilación de sombreadores GSH CafeGLSL y Tablas 3D
    SetCurrentAssetLabel("COMPILANDO SOMBREADORES CAFEGLSL Y TABLAS 3D...");
    for (size_t step = 0; step < 16; ++step) {
        m_bytesLoaded.fetch_add(SHADER_SIM_BYTES / 16, std::memory_order_relaxed);
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

    if (m_audioOpened) {
        SDL_PauseAudio(0);
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
    return m_audioOpened;
}

void AudioManager::Shutdown() {
    if (m_loadingThread.joinable()) {
        m_loadingThread.join();
    }
    if (m_audioOpened) {
        AudioEngine3D::Instance().Shutdown();
        SDL_CloseAudio();
        m_audioOpened = false;
    }
}

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

void AudioManager::GenerateFallbackBGMSamples() {
    constexpr int FREQ = 48000;
    constexpr int DURATION_SEC = 6;
    int numFrames = FREQ * DURATION_SEC;
    m_bgmSamples.resize(numFrames * 2); // 2 channels (Stereo)

    for (int i = 0; i < numFrames; ++i) {
        float t = static_cast<float>(i) / static_cast<float>(FREQ);
        float wave1 = std::sin(2.0f * 3.14159265f * 220.0f * t);
        float wave2 = std::sin(2.0f * 3.14159265f * 261.63f * t);
        float wave3 = std::sin(2.0f * 3.14159265f * 329.63f * t);
        float lfo = 0.6f + 0.4f * std::sin(2.0f * 3.14159265f * 0.5f * t);
        float synth = (wave1 * 0.5f + wave2 * 0.3f + wave3 * 0.2f) * lfo;
        int16_t sample = static_cast<int16_t>(synth * 4000.0f);
        m_bgmSamples[i * 2]     = sample; // Left
        m_bgmSamples[i * 2 + 1] = sample; // Right
    }
}

bool AudioManager::LoadBGM(const std::string& filepath) {
    std::vector<int16_t> loadedBGM;
    if (LoadWAVHelper(filepath, loadedBGM, 2)) {
        SDL_LockAudio();
        m_bgmSamples = std::move(loadedBGM);
        m_bgmPlayIndex = 0;
        m_bgmPlaying = true;
        SDL_UnlockAudio();
        return true;
    }

    // Fallback: Sintetizador estéreo
    SDL_LockAudio();
    GenerateFallbackBGMSamples();
    m_bgmPlayIndex = 0;
    m_bgmPlaying = true;
    SDL_UnlockAudio();
    {
        std::lock_guard<std::mutex> lock(m_stringMutex);
        m_statusString += " | SYNTH FALLBACK USED";
    }
    return false;
}

void AudioManager::PlayBGM() {
    SDL_LockAudio();
    m_bgmPlaying = true;
    SDL_UnlockAudio();
    if (m_audioOpened && m_musicEnabled) {
        SDL_PauseAudio(0);
    }
}

void AudioManager::RestartBGM() {
    SDL_LockAudio();
    m_bgmPlayIndex = 0;
    m_bgmPlaying = true;
    SDL_UnlockAudio();
    if (m_audioOpened && m_musicEnabled) {
        SDL_PauseAudio(0);
    }
}

void AudioManager::StopBGM() {
    SDL_LockAudio();
    m_bgmPlaying = false;
    m_bgmPlayIndex = 0;
    SDL_UnlockAudio();
}

void AudioManager::SetBGMVolume(float volume) {
    SDL_LockAudio();
    m_bgmVolume = std::clamp(volume, 0.0f, 1.0f);
    SDL_UnlockAudio();
}

void AudioManager::SetMasterVolume(float volume) {
    SDL_LockAudio();
    m_masterVolume = std::clamp(volume, 0.0f, 1.0f);
    SDL_UnlockAudio();
}

void AudioManager::SetMusicEnabled(bool enabled) {
    SDL_LockAudio();
    m_musicEnabled = enabled;
    SDL_UnlockAudio();
    if (enabled) {
        if (m_audioOpened && m_bgmPlaying) {
            SDL_PauseAudio(0);
        }
    }
}

void AudioManager::ToggleMusic() {
    SetMusicEnabled(!m_musicEnabled);
}

void AudioManager::SetPaused(bool paused) {
    SDL_LockAudio();
    m_isPaused.store(paused, std::memory_order_release);
    SDL_UnlockAudio();
}

void AudioManager::PlayClick() {
    SDL_LockAudio();
    m_sfxPlayIndex = 0;
    m_sfxPlaying = true;
    SDL_UnlockAudio();
    if (m_audioOpened) {
        SDL_PauseAudio(0);
    }
}

void AudioManager::AudioCallback(void* userdata, Uint8* stream, int len) {
    std::memset(stream, 0, len);
    auto* self = static_cast<AudioManager*>(userdata);
    if (!self) return;

    if (self->m_isPaused.load(std::memory_order_acquire)) {
        // En pausa: congelar avance de cursores de BGM y AudioEngine3D (silencio de escena).
        // Si hay un efecto SFX de UI (click de botón del modal de pausa), permitir su emisión táctil:
        if (self->m_sfxPlaying && !self->m_clickSamples.empty()) {
            int16_t* out = reinterpret_cast<int16_t*>(stream);
            int totalSamples = len / static_cast<int>(sizeof(int16_t));
            int numFrames = totalSamples / 2;
            const float masterVol = self->m_masterVolume;
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

    const float masterVol = self->m_masterVolume;
    const bool musicOn = self->m_musicEnabled && self->m_bgmPlaying;

    for (int i = 0; i < numFrames; ++i) {
        int32_t mixL = 0;
        int32_t mixR = 0;

        // 1. Música de Fondo (Stereo BGM)
        if (musicOn && !self->m_bgmSamples.empty()) {
            size_t idx = self->m_bgmPlayIndex;
            if (idx + 1 < self->m_bgmSamples.size()) {
                mixL += static_cast<int32_t>(self->m_bgmSamples[idx]     * self->m_bgmVolume);
                mixR += static_cast<int32_t>(self->m_bgmSamples[idx + 1] * self->m_bgmVolume);
            }
            self->m_bgmPlayIndex += 2;
            if (self->m_bgmPlayIndex >= self->m_bgmSamples.size()) {
                self->m_bgmPlayIndex = 0; // Seamless loop!
            }
        }

        // 2. Efecto de Sonido Click (Mono SFX duplicated to Left + Right)
        if (self->m_sfxPlaying && !self->m_clickSamples.empty()) {
            if (self->m_sfxPlayIndex < self->m_clickSamples.size()) {
                int32_t sfxVal = static_cast<int32_t>(self->m_clickSamples[self->m_sfxPlayIndex++]);
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

} // namespace WiiU

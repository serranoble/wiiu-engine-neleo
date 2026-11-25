#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <mmsystem.h>
#include <array>
#include <atomic>
#include <cstdint>
#include <cstring>
#include <thread>

namespace WiiU::Platform {

using HostAudioFillFn = void (*)(void* userData, uint8_t* outPcm, int numBytes);

namespace {

constexpr size_t HOST_AUDIO_NUM_BUFFERS = 3u;
constexpr size_t HOST_AUDIO_FRAMES_PER_BUF = 1024u;
constexpr size_t HOST_AUDIO_CHANNELS = 2u;
constexpr size_t HOST_AUDIO_BYTES_PER_BUF = HOST_AUDIO_FRAMES_PER_BUF * HOST_AUDIO_CHANNELS * sizeof(int16_t); // 4096 B

HWAVEOUT s_waveOut = nullptr;
HANDLE s_audioEvent = nullptr;
std::array<WAVEHDR, HOST_AUDIO_NUM_BUFFERS> s_waveHeaders{};
std::array<std::array<int16_t, HOST_AUDIO_FRAMES_PER_BUF * HOST_AUDIO_CHANNELS>, HOST_AUDIO_NUM_BUFFERS> s_pcmBuffers{};
std::atomic<bool> s_audioThreadRunning{false};
std::thread s_audioThread;
HostAudioFillFn s_fillFn = nullptr;
void* s_fillUserData = nullptr;

void AudioThreadWorker() {
    while (s_audioThreadRunning.load(std::memory_order_acquire)) {
        for (size_t i = 0u; i < HOST_AUDIO_NUM_BUFFERS; ++i) {
            if (!s_audioThreadRunning.load(std::memory_order_acquire)) break;
            if ((s_waveHeaders[i].dwFlags & WHDR_DONE) != 0u) {
                std::memset(s_pcmBuffers[i].data(), 0, HOST_AUDIO_BYTES_PER_BUF);
                if (s_fillFn) {
                    s_fillFn(s_fillUserData,
                             reinterpret_cast<uint8_t*>(s_pcmBuffers[i].data()),
                             static_cast<int>(HOST_AUDIO_BYTES_PER_BUF));
                }
                waveOutWrite(s_waveOut, &s_waveHeaders[i], sizeof(WAVEHDR));
            }
        }
        if (s_audioEvent) {
            WaitForSingleObject(s_audioEvent, 20u);
        } else {
            Sleep(5u);
        }
    }
}

} // namespace

void CloseWin32AudioDevice() noexcept {
    s_audioThreadRunning.store(false, std::memory_order_release);
    if (s_audioEvent) {
        SetEvent(s_audioEvent);
    }
    if (s_audioThread.joinable()) {
        s_audioThread.join();
    }
    if (s_waveOut) {
        waveOutReset(s_waveOut);
        for (size_t i = 0u; i < HOST_AUDIO_NUM_BUFFERS; ++i) {
            if ((s_waveHeaders[i].dwFlags & WHDR_PREPARED) != 0u) {
                waveOutUnprepareHeader(s_waveOut, &s_waveHeaders[i], sizeof(WAVEHDR));
            }
        }
        waveOutClose(s_waveOut);
        s_waveOut = nullptr;
    }
    if (s_audioEvent) {
        CloseHandle(s_audioEvent);
        s_audioEvent = nullptr;
    }
    s_fillFn = nullptr;
    s_fillUserData = nullptr;
}

bool OpenWin32AudioDevice(int sampleRate, int channels, HostAudioFillFn fillFn, void* userData) noexcept {
    CloseWin32AudioDevice();
    if (!fillFn) return false;

    const int safeFreq = (sampleRate > 0) ? sampleRate : 48000;
    const int safeCh = (channels == 1 || channels == 2) ? channels : 2;

    s_audioEvent = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    if (!s_audioEvent) {
        return false;
    }

    WAVEFORMATEX wfx{};
    wfx.wFormatTag = WAVE_FORMAT_PCM;
    wfx.nChannels = static_cast<WORD>(safeCh);
    wfx.nSamplesPerSec = static_cast<DWORD>(safeFreq);
    wfx.wBitsPerSample = 16u;
    wfx.nBlockAlign = static_cast<WORD>(safeCh * 2);
    wfx.nAvgBytesPerSec = static_cast<DWORD>(safeFreq) * static_cast<DWORD>(wfx.nBlockAlign);

    if (waveOutOpen(&s_waveOut, WAVE_MAPPER, &wfx,
                    reinterpret_cast<DWORD_PTR>(s_audioEvent), 0u, CALLBACK_EVENT) != MMSYSERR_NOERROR) {
        CloseHandle(s_audioEvent);
        s_audioEvent = nullptr;
        return false;
    }

    s_fillFn = fillFn;
    s_fillUserData = userData;

    for (size_t i = 0u; i < HOST_AUDIO_NUM_BUFFERS; ++i) {
        std::memset(&s_waveHeaders[i], 0, sizeof(WAVEHDR));
        std::memset(s_pcmBuffers[i].data(), 0, HOST_AUDIO_BYTES_PER_BUF);
        s_waveHeaders[i].lpData = reinterpret_cast<LPSTR>(s_pcmBuffers[i].data());
        s_waveHeaders[i].dwBufferLength = static_cast<DWORD>(HOST_AUDIO_BYTES_PER_BUF);
        if (waveOutPrepareHeader(s_waveOut, &s_waveHeaders[i], sizeof(WAVEHDR)) != MMSYSERR_NOERROR) {
            CloseWin32AudioDevice();
            return false;
        }
        if (waveOutWrite(s_waveOut, &s_waveHeaders[i], sizeof(WAVEHDR)) != MMSYSERR_NOERROR) {
            CloseWin32AudioDevice();
            return false;
        }
    }

    s_audioThreadRunning.store(true, std::memory_order_release);
    s_audioThread = std::thread(AudioThreadWorker);
    return true;
}

} // namespace WiiU::Platform

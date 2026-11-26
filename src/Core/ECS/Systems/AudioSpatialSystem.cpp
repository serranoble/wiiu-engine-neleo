#include "Core/ECS/Systems/AudioSpatialSystem.hpp"
#include "Audio/AudioEngine3D.hpp"

namespace Neleo {

void AudioSpatialSystem::Update(ECSWorld& world) {
    Update(world, 0.0f);
}

void AudioSpatialSystem::Update(ECSWorld& world, float dt) {
    AudioEngine3D& audioEngine = AudioEngine3D::Instance();

    world.ForEach<TransformComponent, AudioEmitterComponent>(
        [&audioEngine, dt](Entity /*entity*/, const TransformComponent& transform, AudioEmitterComponent& audio) {
            // Sincronizar posición tridimensional continua y derivar velocidad física para Doppler 3D
            if (dt > 0.0001f && audio.hasPrevPosition) {
                const float invDt = 1.0f / dt;
                audio.emitter.velocity = (transform.position - audio.prevPosition) * invDt;
            }
            audio.prevPosition = transform.position;
            audio.hasPrevPosition = true;
            audio.emitter.position = transform.position;

            if (audio.isPlaying) {
                if (!audio.voiceHandle.IsValid()) {
                    // 1. Solicitud inicial de reproducción
                    if (audio.clip) {
                        audio.voiceHandle = audioEngine.PlaySound3D(*audio.clip, audio.emitter);
                        audio.isDirty = false;
                    }
                } else if (audioEngine.IsPlaying(audio.voiceHandle)) {
                    // 2. Voz activa en curso: actualizar cinemática y acústica espacial
                    audioEngine.UpdateEmitter(audio.voiceHandle, audio.emitter);
                    audio.isDirty = false;
                } else {
                    // 3. El canal ya no está activo en el motor de audio
                    if (!audio.emitter.isLooping) {
                        // Sonido One-Shot finalizado: concluir ciclo de vida deterministamente
                        audio.isPlaying = false;
                        audio.voiceHandle = SoundHandle{};
                        audio.isDirty = false;
                    } else {
                        // Sonido en bucle que fue robado o desalojado: intentar re-adquirir voz
                        if (audio.clip) {
                            audio.voiceHandle = audioEngine.PlaySound3D(*audio.clip, audio.emitter);
                            audio.isDirty = false;
                        }
                    }
                }
            } else {
                // 4. Detención explícita solicitada
                if (audioEngine.IsPlaying(audio.voiceHandle)) {
                    audioEngine.StopSound(audio.voiceHandle);
                    audio.voiceHandle = SoundHandle{};
                    audio.isDirty = false;
                }
            }
        }
    );
}

} // namespace Neleo

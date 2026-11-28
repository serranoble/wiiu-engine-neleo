#pragma once

/**
 * @file NeleoEngine.hpp
 * @brief Umbrella Header público del motor 3D multiplataforma Neleo (Nintendo Wii U GX2 + Windows x64 OpenGL 3.3 Core, C++20).
 *
 * Incluye con una sola directiva la API pública de las 8 librerías estáticas del motor (namespace Neleo):
 * - engine_core: Memoria determinista, PAL (Window/PlatformClock), JobSystem, VFS, CVars, ALSM, ECS, AssetManager, Eventos, Reloj, BVH, Storage/HMAC.
 * - engine_physics: SpatialHashGrid, Colisiones, Física de Cuerpos Rígidos/XPBD, KCC-FSM y CameraRelativeMovement.
 * - engine_ai: Navegación NavGraph, A* determinista, Steering, Behavior Trees y AIPerceptionSystem.
 * - engine_net: Sockets HAL no bloqueantes UDP/TCP (nsysnet/ws2_32), NetBitStream Big-Endian, NetSession y NetReplication.
 * - engine_audio: AudioManager (Streaming BGM <= 256 KB) y AudioEngine3D (128 voces virtuales / 32 físicas, Doppler, Reverb).
 * - engine_input: InputManager, InputContextStack, HID Auto-Pause, ActionMapping, Hápticos y SoftwareKeyboard.
 * - engine_graphics: ForwardRendererGX2/GL (PBR Lite + Uber + Instancing + Early-Z), ShadowMapRenderer, PostProcessHDR, RenderQueue, MaterialSystem, SkyboxRenderPass, RHI y UIToolkit.
 * - engine_framework: Engine, SceneManager, ModalStack, GameScene3D, OrbitCameraSystem, HUDOverlay e IScene.
 */

// 1. Core, Memoria, Concurrencia y Configuracion
#include "Core/Engine.hpp"
#include "Core/ApplicationLifecycle.hpp"
#include "Core/Window.hpp"
#include "Core/MemoryManager.hpp"
#include "Core/JobSystem.hpp"
#include "Core/AssetManager.hpp"
#include "Core/ResourceHandle.hpp"
#include "Core/ResourceMonitor.hpp"
#include "Core/StackString.hpp"
#include "Core/ArenaVector.hpp"
#include "Core/Config/EngineConfig.hpp"
#include "Core/Config/CVarRegistry.hpp"
#include "Core/Localization/LocalizationManager.hpp"
#include "Core/Storage/StorageManager.hpp"
#include "Core/Storage/CryptoHMAC.hpp"
#include "Core/Storage/VirtualFileSystem.hpp"
#include "Core/Time/GameClock.hpp"
#include "Core/Events/GameplayEventBus.hpp"
#include "Core/Math/PRNG.hpp"
#include "Core/Spatial/FrustumTypes.hpp"
#include "Core/Spatial/SpatialBVH.hpp"

// 2. Entity-Component-System (ECS)
#include "Core/ECS/Entity.hpp"
#include "Core/ECS/ComponentPool.hpp"
#include "Core/ECS/ECSWorld.hpp"

#include "Core/ECS/Components/TransformComponent.hpp"
#include "Core/ECS/Components/HierarchyComponent.hpp"
#include "Core/ECS/Components/MeshComponent.hpp"
#include "Core/ECS/Components/MaterialComponent.hpp"
#include "Core/ECS/Components/SkyboxComponent.hpp"
#include "Core/ECS/Components/LightComponent.hpp"
#include "Core/ECS/Components/CameraComponent.hpp"
#include "Core/ECS/Components/SpringArmComponent.hpp"
#include "Core/ECS/Components/BoundingVolumeComponent.hpp"
#include "Core/ECS/Components/ColliderComponent.hpp"
#include "Core/ECS/Components/TriggerComponent.hpp"
#include "Core/ECS/Components/RigidBodyComponent.hpp"
#include "Core/ECS/Components/KinematicCharacterComponent.hpp"
#include "Core/ECS/Components/SkeletalMeshComponent.hpp"
#include "Core/ECS/Components/AnimatorComponent.hpp"
#include "Core/ECS/Components/IKRigComponent.hpp"
#include "Core/ECS/Components/AudioEmitterComponent.hpp"
#include "Core/ECS/Components/StudioTurntableComponent.hpp"
#include "Core/ECS/Components/NetIdentityComponent.hpp"

#include "Core/ECS/Systems/TransformSystem.hpp"
#include "Core/ECS/Systems/CameraSystem.hpp"
#include "Core/ECS/Systems/SpringArmSystem.hpp"
#include "Core/ECS/Systems/OrbitCameraSystem.hpp"
#include "Core/ECS/Systems/StudioTurntableSystem.hpp"
#include "Core/ECS/Systems/CullingSystem.hpp"
#include "Core/ECS/Systems/MeshRenderSystem.hpp"
#include "Core/ECS/Systems/CollisionSystem.hpp"
#include "Core/ECS/Systems/RigidBodyPhysicsSystem.hpp"
#include "Core/ECS/Systems/KinematicCharacterSystem.hpp"
#include "Core/ECS/Systems/CameraRelativeMovementSystem.hpp"
#include "Core/ECS/Systems/AnimationSystem.hpp"
#include "Core/ECS/Systems/IKSystem.hpp"
#include "Core/ECS/Systems/AudioSpatialSystem.hpp"
#include "Core/Physics/CameraRaycastHelper.hpp"
#include "Core/Physics/SpatialHashGrid.hpp"

// 3. Inteligencia Artificial y Navegacion
#include "Core/AI/NavGraph.hpp"
#include "Core/AI/AStarPathfinder.hpp"
#include "Core/AI/SteeringBehaviors.hpp"
#include "Core/AI/AIControllerComponent.hpp"
#include "Core/AI/AISteeringSystem.hpp"
#include "Core/AI/SensoryBlackboard.hpp"
#include "Core/AI/AIPerceptionComponent.hpp"
#include "Core/AI/AIPerceptionSystem.hpp"
#include "Core/AI/BehaviorTree.hpp"
#include "Core/AI/BehaviorTreeComponent.hpp"
#include "Core/AI/BehaviorTreeSystem.hpp"

// 4. Audio 3D y Entrada (HID / Hapticos)
#include "Audio/Audio3DTypes.hpp"
#include "Audio/AudioManager.hpp"
#include "Audio/AudioEngine3D.hpp"
#include "Input/VirtualGamepad.hpp"
#include "Input/ActionMapping.hpp"
#include "Input/InputContextStack.hpp"
#include "Input/InputManager.hpp"
#include "Input/RumbleManager.hpp"
#include "Input/SoftwareKeyboard.hpp"

// 5. Graficos, Mallas, Texturas, Animacion, RHI y UI
#include "Graphics/Color.hpp"
#include "Graphics/Math3D.hpp"
#include "Graphics/Light.hpp"
#include "Graphics/Render2D.hpp"
#include "Graphics/Render2DGX2.hpp"
#include "Graphics/DebugDraw3D.hpp"
#include "Graphics/MaterialSystem.hpp"
#include "Graphics/RenderQueue.hpp"
#include "Graphics/ForwardRendererGX2.hpp"
#include "Graphics/ShadowSystem.hpp"
#include "Graphics/ShadowMapRendererGX2.hpp"
#include "Graphics/SkyboxRenderPass.hpp"
#include "Graphics/Mesh/MeshBinaryFormat.hpp"
#include "Graphics/Mesh/StaticMesh.hpp"
#include "Graphics/Texture/TextureLoaderGX2.hpp"
#include "Graphics/Animation/Skeleton.hpp"
#include "Graphics/Animation/AnimationClip.hpp"
#include "Graphics/Animation/AnimationLoader.hpp"
#include "Graphics/RHI/RHIPipelineState.hpp"
#include "Graphics/RHI/RHIPipelineCache.hpp"
#include "Graphics/RHI/RHIRenderPass.hpp"
#include "Graphics/RHI/RHIFullscreenShader.hpp"
#include "Graphics/RHI/DisplayCompositor.hpp"
#include "Graphics/UIComponents.hpp"
#include "Graphics/UI/UIToolkit.hpp"

// 6. Redes y Multijugador (Cap. 20 Jason Gregory)
#include "Net/NetTypes.hpp"
#include "Net/NetBitStream.hpp"
#include "Net/NetSocket.hpp"
#include "Net/NetSession.hpp"
#include "Net/NetReplication.hpp"

// 7. Framework de Escenas y Juegos
#include "Scene/IScene.hpp"
#include "Scene/ModalStack.hpp"
#include "Scene/SceneManager.hpp"
#include "Scene/GameScene3D.hpp"
#include "Scene/HUDOverlay.hpp"
#include "Scene/SceneLoading.hpp"
#include "Scene/SceneAssetError.hpp"

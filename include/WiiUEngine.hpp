#pragma once

/**
 * @file WiiUEngine.hpp
 * @brief Umbrella Header público del motor 3D Wii U (GX2 / PowerPC Espresso C++20).
 *
 * Incluye con una sola directiva la API pública de todas las librerías estáticas del motor:
 * - engine_core: Memoria determinista, JobSystem, ECS, AssetManager, Eventos, Reloj, BVH, Storage.
 * - engine_physics: Colisiones, Física de Cuerpos Rígidos y Kinematic Character Controller.
 * - engine_ai: Navegación NavGraph, A* determinista y Comportamientos de Dirección.
 * - engine_audio: AudioManager y AudioEngine3D espacializado con Doppler y buses.
 * - engine_input: InputManager (GamePad DRC VPAD, Pro Controller WPAD, Wiimote), ActionMapping y Hápticos.
 * - engine_graphics: ForwardRendererGX2, StaticMesh, TextureLoaderGX2, Animación Esquelética, RHI y UIToolkit.
 * - engine_framework: Engine, SceneManager, GameScene3D, HUDOverlay e IScene.
 */

// 1. Core, Memoria, Concurrencia y Configuracion
#include "Core/Engine.hpp"
#include "Core/Window.hpp"
#include "Core/MemoryManager.hpp"
#include "Core/JobSystem.hpp"
#include "Core/AssetManager.hpp"
#include "Core/ResourceHandle.hpp"
#include "Core/ResourceMonitor.hpp"
#include "Core/StackString.hpp"
#include "Core/ArenaVector.hpp"
#include "Core/Config/EngineConfig.hpp"
#include "Core/Storage/StorageManager.hpp"
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
#include "Core/ECS/Systems/AnimationSystem.hpp"
#include "Core/ECS/Systems/IKSystem.hpp"
#include "Core/ECS/Systems/AudioSpatialSystem.hpp"
#include "Core/Physics/CameraRaycastHelper.hpp"

// 3. Inteligencia Artificial y Navegacion
#include "Core/AI/NavGraph.hpp"
#include "Core/AI/AStarPathfinder.hpp"
#include "Core/AI/SteeringBehaviors.hpp"
#include "Core/AI/AIControllerComponent.hpp"
#include "Core/AI/AISteeringSystem.hpp"

// 4. Audio 3D y Entrada (HID / Hapticos)
#include "Audio/Audio3DTypes.hpp"
#include "Audio/AudioManager.hpp"
#include "Audio/AudioEngine3D.hpp"
#include "Input/VirtualGamepad.hpp"
#include "Input/ActionMapping.hpp"
#include "Input/InputManager.hpp"
#include "Input/RumbleManager.hpp"

// 5. Graficos, Mallas, Texturas, Animacion, RHI y UI
#include "Graphics/Color.hpp"
#include "Graphics/Math3D.hpp"
#include "Graphics/Light.hpp"
#include "Graphics/GX2StateHelper.hpp"
#include "Graphics/Render2D.hpp"
#include "Graphics/Render2DGX2.hpp"
#include "Graphics/DebugDraw3D.hpp"
#include "Graphics/ForwardRendererGX2.hpp"
#include "Graphics/ShadowSystem.hpp"
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

// 6. Framework de Escenas y Juegos
#include "Scene/IScene.hpp"
#include "Scene/SceneManager.hpp"
#include "Scene/GameScene3D.hpp"
#include "Scene/HUDOverlay.hpp"
#include "Scene/SceneLoading.hpp"
#include "Scene/SceneAssetError.hpp"

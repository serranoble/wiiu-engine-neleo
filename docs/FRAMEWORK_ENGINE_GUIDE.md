# Guía del Framework y Subsistemas del Motor Wii U (`engine_*`)

## Subsistema de Física de Cuerpos Rígidos, Restricciones XPBD y KCC (Cap. 14 — Hito 131)

Implementado en la librería estática `engine_physics` (C++20 puro, `0` cabeceras `<gx2/*.h>`, `0` alocaciones en heap durante el bucle de simulación).

### 1. Componentes ECS (`include/Core/ECS/Components/`)
- **`ColliderComponent` (`ColliderComponent.hpp`):**
  - Incorpora `Layer_DynamicBody = 1 << 4` para filtrar cuerpos dinámicos frente a `Layer_StaticWorld` y `Layer_Player`.
- **`RigidBodyComponent` (`RigidBodyComponent.hpp`):**
  - Tipos de movimiento: `RigidBodyMotionType::{Static, Kinematic, Dynamic}`.
  - Propiedades físicas: `mass`, `invMass`, diagonal local del tensor de inercia inverso `invInertiaLocal`, orientación en $SO(3)$ mediante `Quaternion orientation`, `linearVelocity`, `angularVelocity`, `forceAccum`, `torqueAccum`, `restitution`, `friction`, `linearDamping`, `angularDamping` y `gravityScale`.
  - Hibernación: `sleepTimer`, `isSleeping`, `canSleep`, y método `WakeUp()`.
  - Métodos inline `noexcept`: `ApplyCentralForce`, `ApplyForceAtPoint`, `ApplyLinearImpulse`, `ApplyAngularImpulse`, `ApplyImpulseAtOffset`, `ComputeWorldInvInertia`.
  - Factorías estáticas: `MakeStatic`, `MakeKinematic`, `MakeDynamicSphere`, `MakeDynamicBox`.

### 2. Solver y Sistema Físico (`RigidBodyPhysicsSystem.hpp` / `.cpp`)
- **Capacidad pre-alocada (0 heap allocs):** `MAX_BODY_PROXIES = 128`, `MAX_CONTACTS = 256`, `MAX_CONSTRAINTS = 64` sobre arreglos `std::array`.
- **Pipeline por frame (`RigidBodyPhysicsSystem::Update`):**
  1. `BuildProxyList`: recolecta entidades vivas sólidas con `RigidBodyComponent` o arquitectura de `Layer_StaticWorld`.
  2. Por cada substep (`m_substeps = 2`):
     - `ApplySpringForces`: evalúa muelles de Hooke + amortiguamiento viscoso (`PhysicsConstraintType::Spring`).
     - `IntegrateVelocities`: integración Semi-Implicit Euler de fuerzas/torques con amortiguamiento y límite de velocidad lineal/angular.
     - `GenerateContacts`: detección estrecha analítica `IntersectSphereSphere`, `IntersectSphereAABB` e `IntersectAABBAABB` (SAT de 3 ejes).
     - `ResolveKCCInteractions`: detección cápsula vertical KCC $\leftrightarrow$ rígidos dinámicos con empuje bidireccional y transferencia de momento.
     - `SolveVelocityContacts`: solver iterativo *Sequential Impulses* (Erin Catto, 8 iteraciones) con estabilización de Baumgarte y cono de fricción de Coulomb en 2 ejes tangenciales ortogonales.
     - `IntegratePositions`: traslación lineal y actualización de orientación cuaterniónica, escribiendo directamente `tf.worldMatrix = Matrix4x4::FromTRS(tf.position, rb.orientation, tf.scale)` con `tf.isDirty = false`.
     - `SolveXPBDConstraints`: proyección posicional XPBD + amortiguamiento de velocidad radial para `PhysicsConstraintType::Distance` y `BallSocketPin`.
  3. `UpdateSleepingAndClearAccumulators`: evalúa umbrales de reposo para hibernación automática, limpia acumuladores y publica `PhysicsTelemetry`.
- **Utilidades:** `UpdateKinematicCharactersFlat`, `ApplyRadialExplosion`, `WakeAllBodies`.

### 3. Soporte en Renderer y Escena Showcase
- **`ForwardRendererGX2` (`ForwardRendererGX2.hpp/.cpp`):** Enlaza una textura neutra 4×4 `(0,0,0,0)` en búfer estático alineado a 256 bytes cuando `diffuseTexture == nullptr` (activando `vColor.rgb` en `advanced_mesh_gx2.frag`) y selecciona `GX2_INDEX_TYPE_U32` cuando `mesh.GetHeader().indexFormat == 1`.
- **`SceneRigidBodyPhysicsGX2` (`SceneRigidBodyPhysicsGX2.hpp/.cpp`):** Escena agnóstica de plataforma (Opción 8 en `SceneMenuModels`) con carga asíncrona en Core 2 (`LoadAsync`), pirámide de 10 cajas, péndulo XPBD de 3 eslabones, oscilador por muelle, ragdoll de 5 cuerpos, 3 esferas de restitución variable, KCC jugable, proyectiles y onda expansiva.

---

## Subsistema de Animación Esquelética, Cross-Fade SQT, Layer Masking e Inverse Kinematics (Cap. 12 — Hito 132)

Implementado en la librería estática `engine_core` (C++20 puro, `0` cabeceras `<gx2/*.h>`, `0` alocaciones en heap durante el bucle de actualización).

### 1. Matemáticas 3D y Pose SQT (`Math3D.hpp`, `AnimationClip.hpp`, `SkeletalMeshComponent.hpp`)
- **`Quaternion::FromToRotation` & `Matrix4x4::ToQuaternion` (`Math3D.hpp`):** Cálculo robusto del arco mínimo entre direcciones con guarda antiparalela y extracción de orientación desde matrices columna con normalización por columna.
- **`JointTransform` (`AnimationClip.hpp`):** Representación Scale-Quaternion-Translation (`translation`, `rotation`, `scale`) con `JointTransform::Blend` (LERP + `Quaternion::Slerp`) y muestreo directo `AnimationClip::SampleSQTPose` antes de expandir a `Matrix4x4::FromTRS`.
- **`SkeletalMeshComponent::RecomputeWorldAndSkinningMatrices()`:** Almacena `localSQT[MAX_SKELETON_JOINTS]` en `SkeletalPoseData` y recalcula `worldPoses` y `skinningMatrices` (`alignas(256)`) en un solo bucle lineal $\mathcal{O}(N)$ de orden topológico.

### 2. Blending y Control de Capas (`AnimatorComponent.hpp`, `AnimationSystem.cpp`)
- **Cross-Fade en espacio SQT:** `AnimatorComponent::CrossFadeTo(clip, duration, loop)` interpola progresivamente `currentClip` y `nextClip` con factor *smoothstep* ($t^2(3 - 2t)$) sin deformar volúmenes ni escalas, promoviendo automáticamente `nextClip` al completar la duración.
- **Partial-Body Layer Masking:** `AnimatorComponent::SetOverlayLayer(clip, jointMask, weight, loop)` superpone clips parciales filtrados por máscara de bits de 32 bits (ej. `0x03FEu` para animar torso y brazos con `Wave` mientras las piernas siguen caminando con `Walk`).
- **Muestreo estable bajo pausa:** `AnimationSystem::Update` reconstruye la pose base SQT incluso con el clip pausado para que el post-procesamiento IK nunca acumule rotaciones residuales cuadro a cuadro.

### 3. Solvers de Cinemática Inversa (`IKRigComponent.hpp`, `IKSystem.hpp/.cpp`)
- **`IKRigComponent`:** Arreglos de tamaño fijo con `AddTwoBoneChain` (hasta `4` cadenas), `ConfigureLookAt` y `ConfigureFABRIK` (`MAX_CHAIN_JOINTS = 8`).
- **`IKSystem::Update` (`src/Core/ECS/Systems/IKSystem.cpp`):**
  - **Two-Bone IK Analítico:** Ley del Coseno en el plano definido por el vector polo (`poleVectorModel`) con conjugación exacta de rotaciones delta al espacio local del padre y ajuste de pelvis (`pelvisOffsetY`).
  - **Look-At / Aim IK:** Orientación escalonada de pecho y cabeza (`chestWeight`, `headWeight`) hacia un objetivo 3D acotada por `maxAngleRad`.
  - **FABRIK Multi-Articulación:** Iteraciones *Backward & Forward Reaching* en memoria de pila para cadenas de hasta 8 articulaciones, con reconstrucción topológica de matrices FK/Skinning.
- **Demostración en vivo (`SceneSkeletalAnimationGX2`):** Transiciones por Cross-Fade (`A`/`B`/`X`), mezcla parcial `Walk+Wave` (`L` / táctil) y ciclo de modos IK (`OFF` $\rightarrow$ `2-Bone Foot + Look-At` $\rightarrow$ `Full + FABRIK`, botón `R` / táctil) con visualización `DebugDraw3D` de huesos, escalón procedimental y objetivos 3D.

---

## Subsistemas de Bucle Temporal, Cámaras, Bus de Eventos y Audio 3D Avanzado (Caps. 7, 17, 18 y 15 — Hito 133)

Todos los componentes de núcleo residen en `engine_core` / `engine_physics` / `engine_audio` con **0 cabeceras `<gx2/*.h>`** y **0 alocaciones dinámicas en heap** en tiempo de ejecución.

### 1. Reloj Jerárquico y Paso Fijo (`GameClock.hpp`)
- **`GameClock` (`include/Core/Time/GameClock.hpp`):** Reloj *header-only* con separación entre `RealTime` (HUD/cámara en pausa) y `GameplayTime` escalable mediante `timeScale` (*Bullet-Time* `0.25x`).
- **Determinismo & Depuración:** Paso fijo `FIXED_PHYSICS_DT = 1.0f / 60.0f`, guarda contra picos `MAX_RAW_DT = 0.1f`, límite `MAX_PHYSICS_STEPS = 4`, cálculo continuo de `interpolationAlpha`, `TriggerHitStop(duration)` y avance cuadro a cuadro `RequestSingleStep()` mientras el juego está en pausa.

### 2. Bus de Eventos Gameplay Zero-Alloc (`GameplayEventBus.hpp`)
- **`GameplayEventBus` (`include/Core/Events/GameplayEventBus.hpp`):** Cola circular FIFO sobre almacenamiento fijo (`MAX_QUEUED_EVENTS = 64`) para `GameplayEventType` (`Publish`, `Poll`) acompañada de gestor de notificaciones HUD (`MAX_TOASTS = 3`, `UpdateToasts`) sin memoria dinámica.

### 3. Cámaras Orbitales, Spring-Arm DRY, Trauma Shake y Rieles Spline (`CameraRaycastHelper.hpp`, `OrbitCameraSystem`, `SpringArmSystem`)
- **`CameraRaycastHelper` (`include/Core/Physics/CameraRaycastHelper.hpp`):** Helper analítico compartido (`IntersectRaySphere`, Kay-Kajiya *slabs* `IntersectRayAABB` y `SweepWorld`) utilizado tanto por `SpringArmSystem` como por `OrbitCameraSystem`.
- **`OrbitCameraSystem` (`include/Core/ECS/Systems/OrbitCameraSystem.hpp/.cpp`):** Incluye colisión activa de brazo (`SetCollisionEnabled`, `GetEffectiveDistance`, `IsSpringArmCompressing`), anticipación de velocidad (`SetTargetVelocity`, `GetLookAheadOffset`), sacudida cuadrática $T^2$ (`AddTrauma`, `GetTrauma`) y riel cerrado Catmull-Rom $C^1$ (`MAX_SPLINE_POINTS = 8`, `SetSplineControlPoints`, `SetSplineRailActive`, `EvaluateSplineAt`).

### 4. Audio Espacial 3D: Doppler, Sidechain Ducking y Reverberación Schroeder (`Audio3DTypes.hpp`, `AudioSpatialSystem`, `AudioEngine3D`)
- **`AudioSpatialSystem` (`include/Core/ECS/Systems/AudioSpatialSystem.hpp/.cpp`):** Calcula velocidades de emisores mediante diferencias finitas (`prevPosition` / `hasPrevPosition`) sobre `AudioEmitterComponent`.
- **`AudioEngine3D` (`include/Audio/AudioEngine3D.hpp/.cpp`):** Implementa efecto Doppler físico ($c = 343.0\text{ m/s}$ con denominador acotado `std::max(1.0f, SPEED_OF_SOUND + vS)`), muestreo `samplePhase` fraccional con interpolación lineal en `MixAudio`, buses de mezcla (`AudioBus`: `Master`, `BGM`, `SFX`, `Ambience`) con Sidechain Ducking (`targetDuck = 0.38f`) al dispararse `SFX` prioritarios, y reverberación estéreo Schroeder sobre búfer circular estático (`REVERB_DELAY_FRAMES = 2048`, líneas peine primas `1553` / `1913`, `MIX_CHUNK_FRAMES = 512`).
- **Escenas de Validación:** Opción 5 (`SceneCollisionTriggerDemoGX2`) integra el showcase completo (dron orbital Doppler, zona de reverb del Obelisco, *Bullet-Time*, *Riel Spline C1*, *Single-Step*, Hit-Stop, Toasts y Sidechain Ducking); Opción 8 (`SceneRigidBodyPhysicsGX2`) expone `ALPHA` de paso fijo y `TRAUMA` Shake en proyectiles (`A`) y explosión radial (`X`).

---

## Refactorización para Modularidad, Reusabilidad y API Pública (Hito 134 — `OPT-418`..`OPT-420`)

Diseñado para permitir que cualquier juego homebrew o proyecto de terceros enlace directamente las librerías estáticas `engine_*` sin depender de las escenas de demostración.

### 1. Desacoplamiento Estricto Motor $\leftrightarrow$ Demos (`OPT-418`)
- **`MaterialComponent.hpp` sin `<gx2/texture.h>`:** Sustituido por *forward declaration* (`struct GX2Texture;`), logrando **`0` inclusiones `<gx2/*.h>` en todo `include/Core/`**.
- **`SceneManager` basado en fábricas (`SceneManager.hpp/.cpp`):** Eliminadas todas las inclusiones de `SceneMenu*` y `SceneOptions` desde `engine_framework` y `engine_graphics`. Las rutas de navegación se registran externamente vía `SetHomeMenuFactory`, `SetModelsMenuFactory` y `SetOptionsSceneFactory`, consumidas por `NavigateToHomeMenu()`, `NavigateToParentMenu(fromPureGX2)` y `NavigateToOptions(fromPureGX2)`.
- **`Engine::Init(std::unique_ptr<IScene> initialScene = nullptr)`:** Permite iniciar directamente cualquier `IScene` o `GameScene3D` de un juego usuario, conservando el arranque por defecto hacia `SceneLoading` cuando se omite el parámetro.
- **`ResourceMonitor.cpp` movido a `engine_framework`:** Desliga completamente a `engine_core` de cualquier símbolo de UI/overlay.

### 2. Reusabilidad de Texturas, CubeMaps y Skinned Meshes (`OPT-419`)
- **`Graphics::TextureLoaderGX2` (`include/Graphics/Texture/TextureLoaderGX2.hpp` / `src/Graphics/Texture/TextureLoaderGX2.cpp`):** Encapsula en `engine_graphics` la carga alineada en `MEM2` y flush DMA (`GX2Invalidate`) para texturas 2D cocinadas `.wtex` (`RGBA8`, `BC1`, `BC3`), CubeMaps de 6 caras `.wcube`, texturas sólidas 4×4 y destrucción segura `DestroyTexture(GX2Texture&)`.
- **`GameScene3D` con pool integrado y RAII automático (`GameScene3D.hpp/.cpp`):** Incorpora helpers protegidos `LoadSceneTexture`, `LoadSceneTextureFromMemory`, `CreateSceneSolidTexture`, `LoadSceneCubeMap`, `LoadSceneCubeMapFromMemory`, `GetSceneTexture`, `GetSceneCubeMap` y `ReleaseSceneTextures()`. Todas las texturas creadas desde una escena hija se destruyen automáticamente tras `GX2DrawDone()` en `GameScene3D::DestroyGX2Pipeline()`.
- **`ForwardRendererGX2::RenderSkinnedMesh(const StaticMesh&, ...)`:** Sobrecarga directa sobre `StaticMesh` que evita manipular `GX2RBuffer` crudos en escenas con personajes articulados (LBS).
- **Escenas refactorizadas:** `SceneAIDemoGX2`, `SceneInputHIDDemoGX2` y `SceneCollisionTriggerDemoGX2` eliminaron por completo código repetido de `.wtex`/`.wcube`/`GX2RBuffer` y todas sus cabeceras `<gx2/*.h>` / `<gx2r/*.h>`.

### 3. Umbrella Header Público y CMake Reutilizable (`OPT-420`)
- **`include/WiiUEngine.hpp`:** Punto de entrada único (`#include "WiiUEngine.hpp"`) que agrupa Core, ECS, Física, IA, Audio 3D, HID, Gráficos GX2/RHI y Framework de Escenas.
- **`CMakeLists.txt`:** Exposición pública automática de cabeceras (`target_include_directories(engine_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)`) y bandera `option(WIIU_ENGINE_BUILD_DEMOS "Build the Wii U Demo Showcase executable (wiiu_demos)" ON)` para integrar el motor vía `add_subdirectory(...)` con `-DWIIU_ENGINE_BUILD_DEMOS=OFF`.

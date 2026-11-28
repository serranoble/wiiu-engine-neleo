cmake_minimum_required(VERSION 3.14)
project(wiiu_demos C CXX)

# La aplicación usa C++20; estos flags de optimización se aplican a todas las configuraciones.
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(MSVC)
    add_compile_options(/O2 /W3)
else()
    # Preservar std::isfinite/std::isnan bajo -O3 -ffast-math (-fno-finite-math-only)
    add_compile_options(-O3 -ffast-math -fno-finite-math-only)
endif()
if(WUT)
    add_compile_definitions(__WIIU__ __wiiu__)
endif()

option(WIIU_ENGINE_BUILD_DEMOS "Build the Wii U Demo Showcase executable (wiiu_demos)" ON)

# Rutas de inclusión compartidas para todo el motor y librerías estáticas
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/include)
if(WUT)
    include_directories(
        $ENV{DEVKITPRO}/portlibs/wiiu/include
        /c/devkitPro/portlibs/wiiu/include
        C:/devkitPro/portlibs/wiiu/include
    )
endif()

# ==============================================================================
# [Hito 119 / 142] MODULARIZACIÓN EN LIBRERÍAS ESTÁTICAS DEL MOTOR (GAME ENGINE FRAMEWORK)
# Arquitectura desacoplada según Jason Gregory (Game Engine Architecture):
# - engine_core: Memoria, JobSystem, VFS, CVars, ALSM, AssetManager, ECS, Matemáticas, Contenedores
# - engine_physics: SpatialHashGrid, Colisiones, KCC-FSM, RigidBody/XPBD, CameraRelativeMovement
# - engine_audio: AudioManager (BGM Streaming <=256KB), AudioEngine3D (128 virtuales/32 fisicas)
# - engine_input: InputManager, InputContextStack, RumbleManager
# - engine_ai: NavGraph, A*, Steering, Behavior Trees y AIPerceptionSystem
# - engine_graphics: Render2DGX2, ForwardRendererGX2, ShadowMapRendererGX2, RenderQueue, MaterialSystem, RHI
# - engine_framework: Engine, SceneManager, ModalStack, GameScene3D, OrbitCameraSystem, HUDOverlay
# ==============================================================================

# 1. CORE DEL MOTOR
set(ENGINE_CORE_PLATFORM_SOURCES)
if(WUT)
    list(APPEND ENGINE_CORE_PLATFORM_SOURCES
        src/Platform/WiiU/PlatformClockWiiU.cpp
        src/Platform/WiiU/WindowWiiU.cpp
    )
elseif(WIN32)
    list(APPEND ENGINE_CORE_PLATFORM_SOURCES
        src/Platform/Windows/PlatformClockWin32.cpp
        src/Platform/Windows/WindowWin32.cpp
    )
endif()

add_library(engine_core STATIC
    src/Core/ApplicationLifecycle.cpp
    src/Core/Storage/CryptoHMAC.cpp
    src/Core/Storage/StorageManager.cpp
    src/Core/Storage/VirtualFileSystem.cpp
    src/Core/Config/EngineConfig.cpp
    src/Core/Config/CVarRegistry.cpp
    src/Core/Localization/LocalizationManager.cpp
    src/Core/MemoryManager.cpp
    src/Core/Diagnostics/CoreSelfTest.cpp
    ${ENGINE_CORE_PLATFORM_SOURCES}
    src/Core/AssetManager.cpp
    src/Core/JobSystem.cpp
    src/Core/Math/PRNG.cpp
    src/Core/Spatial/SpatialBVH.cpp
    src/Core/ECS/ECSWorld.cpp
    src/Core/ECS/Systems/TransformSystem.cpp
    src/Core/ECS/Systems/CameraSystem.cpp
    src/Core/ECS/Systems/CullingSystem.cpp
    src/Core/ECS/Systems/SpringArmSystem.cpp
    src/Core/ECS/Systems/StudioTurntableSystem.cpp
    src/Core/ECS/Systems/AnimationSystem.cpp
    src/Core/ECS/Systems/IKSystem.cpp
)
target_include_directories(engine_core PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)
if(WUT)
    target_compile_definitions(engine_core PUBLIC __WIIU__)
endif()

# 2. SUBSISTEMA DE FÍSICA Y COLISIONES
add_library(engine_physics STATIC
    src/Core/Physics/SpatialHashGrid.cpp
    src/Core/Physics/PhysicsSelfTest.cpp
    src/Core/ECS/Systems/CollisionSystem.cpp
    src/Core/ECS/Systems/KinematicCharacterSystem.cpp
    src/Core/ECS/Systems/CameraRelativeMovementSystem.cpp
    src/Core/ECS/Systems/RigidBodyPhysicsSystem.cpp
)
target_link_libraries(engine_physics PUBLIC engine_core)

# 3. SUBSISTEMA DE AUDIO 3D
set(ENGINE_AUDIO_PLATFORM_SOURCES)
if(WIN32 AND NOT WUT)
    list(APPEND ENGINE_AUDIO_PLATFORM_SOURCES src/Platform/Windows/AudioDeviceWin32.cpp)
endif()
add_library(engine_audio STATIC
    src/Audio/AudioManager.cpp
    src/Audio/AudioEngine3D.cpp
    src/Audio/AudioSelfTest.cpp
    src/Core/ECS/Systems/AudioSpatialSystem.cpp
    ${ENGINE_AUDIO_PLATFORM_SOURCES}
)
target_link_libraries(engine_audio PUBLIC engine_core)

# 4. SUBSISTEMA DE ENTRADA Y CONTROLADORES
set(ENGINE_INPUT_PLATFORM_SOURCES)
if(WIN32 AND NOT WUT)
    list(APPEND ENGINE_INPUT_PLATFORM_SOURCES
        src/Platform/Windows/InputWin32.cpp
        src/Platform/Windows/RumbleWin32.cpp
    )
endif()
add_library(engine_input STATIC
    src/Input/InputContextStack.cpp
    src/Input/InputManager.cpp
    src/Input/RumbleManager.cpp
    src/Input/InputSelfTest.cpp
    ${ENGINE_INPUT_PLATFORM_SOURCES}
)
target_link_libraries(engine_input PUBLIC engine_core)

# 5. SUBSISTEMA DE GRÁFICOS Y RENDERIZADO 2D/3D (LATTE GX2 / OPENGL 3.3)
set(ENGINE_GRAPHICS_BACKEND_SOURCES)
if(WUT)
    list(APPEND ENGINE_GRAPHICS_BACKEND_SOURCES
        src/Graphics/ForwardRendererGX2.cpp
        src/Graphics/Texture/TextureLoaderGX2.cpp
        src/Graphics/ShadowMapRendererGX2.cpp
        src/Graphics/PostProcessHDRGX2.cpp
        src/Graphics/RHI/RenderTargetGX2.cpp
        src/Graphics/RHI/RHIFullscreenShader.cpp
    )
else()
    list(APPEND ENGINE_GRAPHICS_BACKEND_SOURCES
        src/Graphics/GL/GLFunctions.cpp
        src/Graphics/GL/Render2DGL.cpp
        src/Graphics/GL/ForwardRendererGL.cpp
        src/Graphics/GL/TextureLoaderGL.cpp
        src/Graphics/GL/RenderTargetGL.cpp
        src/Graphics/GL/ShadowMapRendererGL.cpp
        src/Graphics/GL/PostProcessHDRGL.cpp
        src/Graphics/GL/RHIFullscreenShaderGL.cpp
    )
endif()
add_library(engine_graphics STATIC
    src/Graphics/Render2D.cpp
    src/Graphics/Render2DGX2.cpp
    ${ENGINE_GRAPHICS_BACKEND_SOURCES}
    src/Graphics/DebugDraw3D.cpp
    src/Graphics/MaterialSystem.cpp
    src/Graphics/RenderQueue.cpp
    src/Graphics/ShadowSystem.cpp
    src/Graphics/SkyboxRenderPass.cpp
    src/Graphics/Mesh/StaticMesh.cpp
    src/Graphics/Mesh/ProceduralMeshBuilder.cpp
    src/Graphics/GraphicsSelfTest.cpp
    src/Graphics/RHI/RHIPipelineCache.cpp
    src/Graphics/RHI/RHIRenderPass.cpp
    src/Graphics/RHI/DisplayCompositor.cpp
    src/Graphics/UIComponents.cpp
    src/Graphics/UI/UIToolkit.cpp
    src/Core/ECS/Systems/MeshRenderSystem.cpp
)
target_link_libraries(engine_graphics PUBLIC engine_core)

# 6. FRAMEWORK DE JUEGOS Y GESTIÓN DE ESCENAS
add_library(engine_framework STATIC
    src/Core/Engine.cpp
    src/Core/ResourceMonitor.cpp
    src/Core/Diagnostics/EngineSelfTestSuite.cpp
    src/Core/ECS/Systems/OrbitCameraSystem.cpp
    src/Input/SoftwareKeyboard.cpp
    src/Scene/ModalStack.cpp
    src/Scene/SceneManager.cpp
    src/Scene/GameScene3D.cpp
    src/Scene/HUDOverlay.cpp
    src/Scene/SceneLoading.cpp
    src/Scene/SceneAssetError.cpp
)

# 7. SUBSISTEMA DE INTELIGENCIA ARTIFICIAL Y NAVEGACIÓN (CAP. 19 JASON GREGORY)
add_library(engine_ai STATIC
    src/Core/AI/NavGraph.cpp
    src/Core/AI/AStarPathfinder.cpp
    src/Core/AI/SteeringBehaviors.cpp
    src/Core/AI/AISteeringSystem.cpp
    src/Core/AI/AIPerceptionSystem.cpp
    src/Core/AI/BehaviorTreeSystem.cpp
    src/Core/AI/AISelfTest.cpp
)
target_link_libraries(engine_ai PUBLIC engine_core)

# 8. SUBSISTEMA DE REDES Y MULTIJUGADOR (CAP. 20 JASON GREGORY)
add_library(engine_net STATIC
    src/Net/NetBitStream.cpp
    src/Net/NetSocket.cpp
    src/Net/NetSession.cpp
    src/Net/NetReplication.cpp
)
target_link_libraries(engine_net PUBLIC engine_core)
if(WIN32 AND NOT WUT)
    target_link_libraries(engine_net PUBLIC ws2_32)
endif()

target_link_libraries(engine_framework PUBLIC 
    engine_graphics 
    engine_physics 
    engine_audio 
    engine_input 
    engine_ai
    engine_net
    engine_core
)

# ==============================================================================
# DEMOSTRADORES Y EJECUTABLE PRINCIPAL (wiiu_demos)
# ==============================================================================
set(DEMO_SOURCES
    src/main.cpp
    src/Scene/Starfield3D.cpp
    src/Scene/SceneMenu.cpp
    src/Scene/SceneOptions.cpp
    src/Scene/NetTestModal.cpp
    src/Scene/GraphicsQAModal.cpp
    src/Scene/GraphicsQASceneBase.cpp
    src/Scene/SceneQA_PipelineTextures.cpp
    src/Scene/SceneQA_LightingPBR.cpp
    src/Scene/SceneQA_Shadows.cpp
    src/Scene/SceneQA_EnvironmentWater.cpp
    src/Scene/SceneQA_PostProcessAlpha.cpp
    src/Scene/SceneQA_StressCulling.cpp
    src/Scene/SceneQA_ProceduralNPR.cpp
    src/Scene/SceneQA_RaytracingBVH.cpp
    src/Scene/SceneModelHouse.cpp
    src/Scene/SceneModelBird.cpp
)

if(WUT AND WIIU_ENGINE_BUILD_DEMOS)
    add_executable(wiiu_demos ${DEMO_SOURCES})

    target_link_libraries(wiiu_demos PRIVATE
        engine_framework
        engine_graphics
        engine_physics
        engine_audio
        engine_input
        engine_ai
        engine_net
        engine_core
        wut
        m
    )
    
    # [OPT-141] Dependencias explicitas de assets para sincronizar y regenerar WUHB ante cualquier cambio
    file(GLOB_RECURSE ENGINE_ASSET_FILES CONFIGURE_DEPENDS
        "${CMAKE_CURRENT_SOURCE_DIR}/assets/*"
    )
    set(ASSETS_SYNC_STAMP "${CMAKE_BINARY_DIR}/.assets_synced.stamp")
    add_custom_command(
        OUTPUT "${ASSETS_SYNC_STAMP}"
        COMMAND ${CMAKE_COMMAND} -E copy_directory
            ${CMAKE_CURRENT_SOURCE_DIR}/assets
            ${CMAKE_BINARY_DIR}/assets
        COMMAND ${CMAKE_COMMAND} -E copy_directory
            ${CMAKE_CURRENT_SOURCE_DIR}/assets
            ${CMAKE_BINARY_DIR}/content
        COMMAND ${CMAKE_COMMAND} -E touch "${ASSETS_SYNC_STAMP}"
        DEPENDS ${ENGINE_ASSET_FILES}
        COMMENT "Sincronizando assets actualizados en build/assets y build/content..."
    )
    add_custom_target(sync_assets ALL DEPENDS "${ASSETS_SYNC_STAMP}")
    add_dependencies(wiiu_demos sync_assets)

    # El RPX es el ejecutable Wii U; el WUHB lo empaqueta para Aroma con assets y metadatos.
    wut_create_rpx(wiiu_demos)
    
    # Manifest y empaquetado para Aroma
    if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/wuhb_meta/manifest.json")
        wut_create_wuhb(wiiu_demos
            NAME "WiiU Demos"
            SHORTNAME "WiiUDemos"
            AUTHOR "Pablo & Kids"
            ICON "${CMAKE_CURRENT_SOURCE_DIR}/wuhb_meta/icon.png"
            CONTENT "${CMAKE_CURRENT_SOURCE_DIR}/assets"
        )
        if(TARGET wiiu_demos_wuhb)
            add_dependencies(wiiu_demos_wuhb sync_assets)
            set_property(TARGET wiiu_demos_wuhb APPEND PROPERTY SOURCES "${ASSETS_SYNC_STAMP}")
        endif()
    endif()
elseif(WIN32 AND NOT WUT AND WIIU_ENGINE_BUILD_DEMOS)
    add_executable(wiiu_demos ${DEMO_SOURCES})

    if(MINGW)
        target_link_libraries(wiiu_demos PRIVATE
            engine_framework
            engine_graphics
            engine_physics
            engine_audio
            engine_input
            engine_ai
            engine_net
            engine_core
            opengl32
            gdi32
            user32
            winmm
            ws2_32
        )
        target_link_options(wiiu_demos PRIVATE
            -static
            -static-libgcc
            -static-libstdc++
        )
    else()
        target_link_libraries(wiiu_demos PRIVATE
            engine_framework
            engine_graphics
            engine_physics
            engine_audio
            engine_input
            engine_ai
            engine_net
            engine_core
            opengl32
            gdi32
            user32
            winmm
            ws2_32
        )
    endif()

    file(GLOB_RECURSE ENGINE_ASSET_FILES CONFIGURE_DEPENDS
        "${CMAKE_CURRENT_SOURCE_DIR}/assets/*"
    )
    set(ASSETS_SYNC_STAMP "${CMAKE_BINARY_DIR}/.assets_synced.stamp")
    add_custom_command(
        OUTPUT "${ASSETS_SYNC_STAMP}"
        COMMAND ${CMAKE_COMMAND} -E copy_directory
            ${CMAKE_CURRENT_SOURCE_DIR}/assets
            ${CMAKE_BINARY_DIR}/assets
        COMMAND ${CMAKE_COMMAND} -E touch "${ASSETS_SYNC_STAMP}"
        DEPENDS ${ENGINE_ASSET_FILES}
        COMMENT "Sincronizando assets para ejecutable Windows en build-win/assets..."
    )
    add_custom_target(sync_assets ALL DEPENDS "${ASSETS_SYNC_STAMP}")
    add_dependencies(wiiu_demos sync_assets)
endif()

cmake_minimum_required(VERSION 3.14)
project(wiiu_demos C CXX)

# La aplicación usa C++20; estos flags de optimización se aplican a todas las configuraciones.
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# La macro en mayúsculas la define WUT; se conserva la minúscula por compatibilidad con guardas existentes.
add_compile_options(-O3 -ffast-math)
add_compile_definitions(__WIIU__ __wiiu__)

option(WIIU_ENGINE_BUILD_DEMOS "Build the Wii U Demo Showcase executable (wiiu_demos)" ON)

# CafeGLSL se compila offline: CMake distribuye los .gsh existentes, no compila los .vert/.frag.
# Copia inicial para que Cemu y las ejecuciones locales encuentren los assets de las demos.
if(WIIU_ENGINE_BUILD_DEMOS)
    file(COPY ${CMAKE_CURRENT_SOURCE_DIR}/assets/ DESTINATION ${CMAKE_BINARY_DIR}/assets)
    file(COPY ${CMAKE_CURRENT_SOURCE_DIR}/assets/ DESTINATION ${CMAKE_BINARY_DIR}/content)
endif()

# Rutas de inclusión compartidas para todo el motor y librerías estáticas
include_directories(
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    $ENV{DEVKITPRO}/portlibs/wiiu/include
    $ENV{DEVKITPRO}/portlibs/wiiu/include/SDL2
    /c/devkitPro/portlibs/wiiu/include
    /c/devkitPro/portlibs/wiiu/include/SDL2
    C:/devkitPro/portlibs/wiiu/include
    C:/devkitPro/portlibs/wiiu/include/SDL2
)

# ==============================================================================
# [Hito 119] MODULARIZACIÓN EN LIBRERÍAS ESTÁTICAS DEL MOTOR (GAME ENGINE FRAMEWORK)
# Arquitectura desacoplada según Jason Gregory (Game Engine Architecture):
# - engine_core: Memoria, JobSystem, AssetManager, ECS, Matemáticas, Contenedores
# - engine_physics: Sistemas de colisión analítica, KCC, triggers y deslizamiento
# - engine_audio: AudioManager, AudioEngine3D y AudioSpatialSystem
# - engine_input: InputManager (GamePad VPAD, Pro Controller WPAD, Touch)
# - engine_graphics: Render2DGX2, ForwardRendererGX2, RHI, UIToolkit, DebugDraw3D, Mallas
# - engine_framework: Engine, SceneManager, GameScene3D, HUDOverlay, Escenas base
# ==============================================================================

# 1. CORE DEL MOTOR
add_library(engine_core STATIC
    src/Core/Storage/StorageManager.cpp
    src/Core/Config/EngineConfig.cpp
    src/Core/MemoryManager.cpp
    src/Core/Window.cpp
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
    src/Core/ECS/Systems/OrbitCameraSystem.cpp
    src/Core/ECS/Systems/AnimationSystem.cpp
    src/Core/ECS/Systems/IKSystem.cpp
)
target_include_directories(engine_core PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)
target_compile_definitions(engine_core PUBLIC __WIIU__)

# 2. SUBSISTEMA DE FÍSICA Y COLISIONES
add_library(engine_physics STATIC
    src/Core/ECS/Systems/CollisionSystem.cpp
    src/Core/ECS/Systems/KinematicCharacterSystem.cpp
    src/Core/ECS/Systems/RigidBodyPhysicsSystem.cpp
)
target_link_libraries(engine_physics PUBLIC engine_core)

# 3. SUBSISTEMA DE AUDIO 3D
add_library(engine_audio STATIC
    src/Audio/AudioManager.cpp
    src/Audio/AudioEngine3D.cpp
    src/Core/ECS/Systems/AudioSpatialSystem.cpp
)
target_link_libraries(engine_audio PUBLIC engine_core)

# 4. SUBSISTEMA DE ENTRADA Y CONTROLADORES
add_library(engine_input STATIC
    src/Input/InputManager.cpp
    src/Input/RumbleManager.cpp
)
target_link_libraries(engine_input PUBLIC engine_core)

# 5. SUBSISTEMA DE GRÁFICOS Y RENDERIZADO 2D/3D (LATTE GX2)
add_library(engine_graphics STATIC
    src/Graphics/Render2D.cpp
    src/Graphics/Render2DGX2.cpp
    src/Graphics/DebugDraw3D.cpp
    src/Graphics/ForwardRendererGX2.cpp
    src/Graphics/ShadowSystem.cpp
    src/Graphics/Mesh/StaticMesh.cpp
    src/Graphics/Texture/TextureLoaderGX2.cpp
    src/Graphics/RHI/RHIPipelineCache.cpp
    src/Graphics/RHI/RHIRenderPass.cpp
    src/Graphics/RHI/RHIFullscreenShader.cpp
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
)
target_link_libraries(engine_ai PUBLIC engine_core)

target_link_libraries(engine_framework PUBLIC 
    engine_graphics 
    engine_physics 
    engine_audio 
    engine_input 
    engine_ai
    engine_core
)

# ==============================================================================
# DEMOSTRADORES Y EJECUTABLE PRINCIPAL (wiiu_demos)
# ==============================================================================
set(DEMO_SOURCES
    src/main.cpp
    src/Scene/Cube3DObject.cpp
    src/Scene/DraggableSquare.cpp
    src/Scene/RingEffect.cpp
    src/Scene/Starfield3D.cpp
    src/Scene/SceneMenu.cpp
    src/Scene/SceneMenuSDL.cpp
    src/Scene/SceneMenuGX2.cpp
    src/Scene/SceneMenuModels.cpp
    src/Scene/SceneOptions.cpp
    src/Scene/SceneLighting.cpp
    src/Scene/SceneParticles.cpp
    src/Scene/SceneInteractive.cpp
    src/Scene/SceneRaytracing.cpp
    src/Scene/SceneRaytracingGPU.cpp
    src/Scene/SceneGX2Native.cpp
    src/Scene/SceneWater.cpp
    src/Scene/SceneInteractiveGX2.cpp
    src/Scene/SceneShadowsGX2.cpp
    src/Scene/SceneEnvironmentGX2.cpp
    src/Scene/SceneStaticModelsGX2.cpp
    src/Scene/SceneSkeletalAnimationGX2.cpp
    src/Scene/SceneBirdAnimationGX2.cpp
    src/Scene/SceneShowcaseGX2.cpp
    src/Scene/SceneCollisionTriggerDemoGX2.cpp
    src/Scene/SceneAIDemoGX2.cpp
    src/Scene/SceneInputHIDDemoGX2.cpp
    src/Scene/SceneRaytracingBVHGX2.cpp
    src/Scene/SceneRigidBodyPhysicsGX2.cpp
)

if(WUT AND WIIU_ENGINE_BUILD_DEMOS)
    add_executable(wiiu_demos ${DEMO_SOURCES})

    target_link_libraries(wiiu_demos PRIVATE
        -Wl,--start-group
        engine_framework
        engine_graphics
        engine_physics
        engine_audio
        engine_input
        engine_ai
        engine_core
        -Wl,--end-group
        SDL2
        wut
        m
    )
    
    # Sincronización continua de assets en cada compilación
    add_custom_target(sync_assets ALL
        COMMAND ${CMAKE_COMMAND} -E copy_directory
            ${CMAKE_CURRENT_SOURCE_DIR}/assets
            ${CMAKE_BINARY_DIR}/assets
        COMMAND ${CMAKE_COMMAND} -E copy_directory
            ${CMAKE_CURRENT_SOURCE_DIR}/assets
            ${CMAKE_BINARY_DIR}/content
        COMMENT "Sincronizando assets en build/assets y build/content junto al ejecutable..."
    )
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
    endif()
endif()

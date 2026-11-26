// Punto de entrada de consola y Cemu. El ámbito local fuerza la destrucción del motor
// antes de retornar, para cerrar escenas, hilos, audio, GX2/SDL y recursos de ventana.

#include "Core/Engine.hpp"
#include "Scene/SceneManager.hpp"
#include "Scene/SceneMenu.hpp"
#include "Scene/SceneOptions.hpp"
#include <memory>
#include <utility>

int main(int argc, char* argv[]) {
    (void)argc;
    (void)argv;
    {
        auto& sceneMgr = Neleo::SceneManager::Instance();
        sceneMgr.SetHomeMenuFactory([]() {
            return std::make_unique<Neleo::SceneMenu>();
        });
        sceneMgr.SetModelsMenuFactory([]() {
            return std::make_unique<Neleo::SceneMenu>();
        });
        sceneMgr.SetOptionsSceneFactory([](std::function<void()> onBack) {
            return std::make_unique<Neleo::SceneOptions>(std::move(onBack));
        });

        Neleo::Engine engine;

        if (engine.Init()) {
            engine.Run();
        }
    }

    return 0;
}

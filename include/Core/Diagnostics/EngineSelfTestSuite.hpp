#pragma once

#include "Core/Diagnostics/SelfTestTypes.hpp"

namespace Neleo::Diagnostics {

/**
 * @brief Orquestador unificado de validaciones del motor (engine_framework).
 *
 * Enlaza las validaciones de las 8 librerías del DAG (engine_core, engine_physics, engine_audio,
 * engine_input, engine_graphics, engine_ai, engine_net, engine_framework) y produce un reporte
 * consolidado Zero-Alloc en stack (EngineSelfTestReport = SelfTestReport<64u>).
 */
class EngineSelfTestSuite {
public:
    /**
     * @brief Ejecuta todas las pruebas de integridad del motor (Fase V2: 28 pruebas / 8 librerías).
     */
    [[nodiscard]] static EngineSelfTestReport RunAll();
};

} // namespace Neleo::Diagnostics

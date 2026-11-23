#pragma once

#include "Core/Diagnostics/SelfTestTypes.hpp"

namespace WiiU::Graphics {

/**
 * @brief Batería de pruebas automatizadas de invariantes gráficos (GX2, RHI, Math3D, Mallas y Mipmaps).
 *
 * Diseñada para ejecutarse en <1 ms al abrir el Laboratorio Gráfico QA o al arranque,
 * detectando de inmediato regresiones críticas como ausencia de GX2DepthBuffer, desalineación MEM1/MEM2,
 * cadenas de mipmaps rotas o hashes RHI inválidos.
 */
class GraphicsSelfTest {
public:
    /**
     * @brief Ejecuta las 8 pruebas de invariantes gráficos y las agrega al reporte.
     */
    static void RunAll(Diagnostics::EngineSelfTestReport& outReport);
};

} // namespace WiiU::Graphics

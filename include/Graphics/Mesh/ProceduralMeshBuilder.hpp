#pragma once

#include "Graphics/Mesh/StaticMesh.hpp"
#include <cstdint>

namespace Neleo::Graphics::ProceduralMeshBuilder {

/**
 * @brief Construye un plano horizontal en Y=0 con normales (+Y) y coordenadas UV escalables.
 */
[[nodiscard]] bool CreatePlane(StaticMesh& outMesh,
                               float width,
                               float depth,
                               float uvScaleX = 1.0f,
                               float uvScaleZ = 1.0f,
                               uint32_t subdivX = 1u,
                               uint32_t subdivZ = 1u,
                               float r = 1.0f,
                               float g = 1.0f,
                               float b = 1.0f);

/**
 * @brief Construye un cubo centrado en el origen con normales ortogonales por cara y UV escalables.
 */
[[nodiscard]] bool CreateCube(StaticMesh& outMesh,
                              float size = 1.0f,
                              float uvScale = 1.0f,
                              float r = 1.0f,
                              float g = 1.0f,
                              float b = 1.0f);

/**
 * @brief Construye una esfera UV paramétrica con normales suaves y coordenadas esféricas (reusable en G1..G8).
 */
[[nodiscard]] bool CreateUVSphere(StaticMesh& outMesh,
                                  float radius = 1.0f,
                                  uint32_t slices = 24u,
                                  uint32_t stacks = 16u,
                                  float r = 1.0f,
                                  float g = 1.0f,
                                  float b = 1.0f);

/**
 * @brief Evalúa de forma determinista la altura del terreno procedural en coordenadas mundo (x, z).
 */
[[nodiscard]] float EvaluateTerrainHeight(float x, float z, float maxHeight = 5.5f) noexcept;

/**
 * @brief Construye una malla de terreno por heightmap con normales analíticas por diferencias centrales
 *        y atributos (altura normalizada en R, pendiente 1-Ny en G) en el color de vértice (CGP Cap. 12).
 */
[[nodiscard]] bool CreateHeightmapTerrain(StaticMesh& outMesh,
                                          float width = 56.0f,
                                          float depth = 56.0f,
                                          uint32_t gridX = 48u,
                                          uint32_t gridZ = 48u,
                                          float maxHeight = 5.5f);

} // namespace Neleo::Graphics::ProceduralMeshBuilder

#pragma once

#include <SDL2/SDL.h>
#include "Graphics/Color.hpp"
#include "Graphics/Math3D.hpp"
#include "Graphics/Light.hpp"
#include "Core/Math/PRNG.hpp"
#include <vector>

namespace WiiU {

struct CubeFace {
    int v[4];
    ColorRGBA baseColor;
    float avgDepthZ{0.0f};
    Point3D rawNormal;
};

class Cube3DObject {
public:
    Cube3DObject();

    void Update(float dt);
    void Render(SDL_Renderer* renderer, int screenWidth, int screenHeight, const DirectionalLight& light);
    void ScrambleFaceColors();
    void ScrambleFaceColors(PRNG& prng);

private:
    Point3D m_vertices[8];
    CubeFace m_faces[6];
    float m_rotX{20.0f};
    float m_rotY{30.0f};
    float m_rotZ{0.0f};
    PRNG m_prng{0x5C8AB1E0ULL};
};

} // namespace WiiU

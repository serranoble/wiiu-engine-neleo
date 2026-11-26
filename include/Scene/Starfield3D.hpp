#pragma once

#include <cstdint>
#include <vector>
#include "Graphics/Math3D.hpp"
#include "Graphics/Color.hpp"
#include "Core/Math/PRNG.hpp"

namespace Neleo {

struct Star {
    Point3D pos;
    float speed;
    float size;
};

/**
 * @brief Campo de estrellas 3D determinista impulsado por PRNG de Jason Gregory (Cap. 4.6).
 * 
 * Reemplaza por completo las llamadas no deterministas a `rand()` por una instancia de `PRNG`.
 */
class Starfield3D {
public:
    explicit Starfield3D(int numStars = 150, uint64_t seed = 0x1337BEEFCAFEULL);

    void ResetSeed(uint64_t seed);
    void Update(float dt);
    void Render(int screenWidth = 1280, int screenHeight = 720);

    [[nodiscard]] uint64_t GetCurrentSeed() const noexcept { return m_initialSeed; }
    [[nodiscard]] const PRNG& GetPRNG() const noexcept { return m_prng; }
    [[nodiscard]] PRNG& GetPRNG() noexcept { return m_prng; }

private:
    uint64_t m_initialSeed{0x1337BEEFCAFEULL};
    PRNG m_prng;
    std::vector<Star> m_stars;
};

} // namespace Neleo

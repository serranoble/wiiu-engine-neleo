#pragma once

#include <cstdint>
#include <cstdlib>

namespace WiiU {

class PRNG;

struct ColorRGBA {
    uint8_t r{255};
    uint8_t g{255};
    uint8_t b{255};
    uint8_t a{255};

    static constexpr ColorRGBA Red()     { return ColorRGBA{235, 55, 55, 255}; }
    static constexpr ColorRGBA Cyan()    { return ColorRGBA{0, 200, 240, 255}; }
    static constexpr ColorRGBA Orange()  { return ColorRGBA{255, 140, 0, 255}; }
    static constexpr ColorRGBA Magenta() { return ColorRGBA{235, 50, 180, 255}; }
    static constexpr ColorRGBA Green()   { return ColorRGBA{40, 220, 90, 255}; }
    static constexpr ColorRGBA Yellow()  { return ColorRGBA{255, 220, 0, 255}; }
    static constexpr ColorRGBA White()   { return ColorRGBA{255, 255, 255, 255}; }
    static constexpr ColorRGBA Black()   { return ColorRGBA{0, 0, 0, 255}; }

    [[deprecated("Use ColorRGBA::Random(PRNG&) for deterministic PRNG per Jason Gregory 4.6")]]
    static ColorRGBA Random() {
        return ColorRGBA{
            static_cast<uint8_t>(60 + std::rand() % 196),
            static_cast<uint8_t>(60 + std::rand() % 196),
            static_cast<uint8_t>(60 + std::rand() % 196),
            255
        };
    }

    /**
     * @brief [OPT-387] Generación de color determinista mediante PRNG de Jason Gregory (Cap. 4.6).
     */
    static ColorRGBA Random(PRNG& prng);
};

} // namespace WiiU

#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string_view>

namespace Neleo {

/**
 * @brief [OPT-398] Primitivas criptograficas FIPS 180-4 (SHA-256) y RFC 2104 (HMAC-SHA256).
 *
 * Implementacion C++20 pura sin dependencias externas, independiente de endianness
 * (compatible con PowerPC Big-Endian de Wii U y ARM/x86 Little-Endian) y con
 * 0 alocaciones en heap.
 */
class CryptoHMAC {
public:
    static constexpr size_t SHA256_DIGEST_SIZE = 32;
    static constexpr size_t SHA256_BLOCK_SIZE  = 64;

    using Digest = std::array<uint8_t, SHA256_DIGEST_SIZE>;

    struct Sha256Context {
        uint32_t state[8]{};
        uint64_t bitCount{0};
        uint8_t buffer[SHA256_BLOCK_SIZE]{};
        size_t bufferLen{0};
    };

    static void Sha256Init(Sha256Context& ctx) noexcept;
    static void Sha256Update(Sha256Context& ctx, const uint8_t* data, size_t len) noexcept;
    static void Sha256Final(Sha256Context& ctx, Digest& outDigest) noexcept;

    [[nodiscard]] static Digest ComputeSha256(const void* data, size_t len) noexcept;

    [[nodiscard]] static Digest ComputeHmacSha256(const void* key, size_t keyLen,
                                                  const void* data, size_t dataLen) noexcept;

    [[nodiscard]] static Digest ComputeHmacSha256(std::string_view key,
                                                  const void* data, size_t dataLen) noexcept {
        return ComputeHmacSha256(key.data(), key.size(), data, dataLen);
    }

    /**
     * @brief Comparacion en tiempo constante para evitar ataques de temporizacion.
     */
    [[nodiscard]] static bool ConstantTimeEqual(const Digest& a, const Digest& b) noexcept;
};

} // namespace Neleo

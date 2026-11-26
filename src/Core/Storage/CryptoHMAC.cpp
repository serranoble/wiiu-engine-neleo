#include "Core/Storage/CryptoHMAC.hpp"
#include <cstring>

namespace Neleo {

namespace {

constexpr uint32_t K_SHA256[64] = {
    0x428a2f98u, 0x71374491u, 0xb5c0fbcfu, 0xe9b5dba5u,
    0x3956c25bu, 0x59f111f1u, 0x923f82a4u, 0xab1c5ed5u,
    0xd807aa98u, 0x12835b01u, 0x243185beu, 0x550c7dc3u,
    0x72be5d74u, 0x80deb1feu, 0x9bdc06a7u, 0xc19bf174u,
    0xe49b69c1u, 0xefbe4786u, 0x0fc19dc6u, 0x240ca1ccu,
    0x2de92c6fu, 0x4a7484aau, 0x5cb0a9dcu, 0x76f988dau,
    0x983e5152u, 0xa831c66du, 0xb00327c8u, 0xbf597fc7u,
    0xc6e00bf3u, 0xd5a79147u, 0x06ca6351u, 0x14292967u,
    0x27b70a85u, 0x2e1b2138u, 0x4d2c6dfcu, 0x53380d13u,
    0x650a7354u, 0x766a0abbu, 0x81c2c92eu, 0x92722c85u,
    0xa2bfe8a1u, 0xa81a664bu, 0xc24b8b70u, 0xc76c51a3u,
    0xd192e819u, 0xd6990624u, 0xf40e3585u, 0x106aa070u,
    0x19a4c116u, 0x1e376c08u, 0x2748774cu, 0x34b0bcb5u,
    0x391c0cb3u, 0x4ed8aa4au, 0x5b9cca4fu, 0x682e6ff3u,
    0x748f82eeu, 0x78a5636fu, 0x84c87814u, 0x8cc70208u,
    0x90befffau, 0xa4506cebu, 0xbef9a3f7u, 0xc67178f2u
};

inline constexpr uint32_t RotRight(uint32_t value, uint32_t bits) noexcept {
    return (value >> bits) | (value << (32u - bits));
}

inline void TransformBlock(CryptoHMAC::Sha256Context& ctx, const uint8_t block[64]) noexcept {
    uint32_t w[64]{};
    for (size_t i = 0; i < 16; ++i) {
        w[i] = (static_cast<uint32_t>(block[i * 4 + 0]) << 24u) |
               (static_cast<uint32_t>(block[i * 4 + 1]) << 16u) |
               (static_cast<uint32_t>(block[i * 4 + 2]) <<  8u) |
               (static_cast<uint32_t>(block[i * 4 + 3]));
    }
    for (size_t i = 16; i < 64; ++i) {
        const uint32_t s0 = RotRight(w[i - 15], 7u) ^ RotRight(w[i - 15], 18u) ^ (w[i - 15] >> 3u);
        const uint32_t s1 = RotRight(w[i - 2], 17u) ^ RotRight(w[i - 2], 19u) ^ (w[i - 2] >> 10u);
        w[i] = w[i - 16] + s0 + w[i - 7] + s1;
    }

    uint32_t a = ctx.state[0];
    uint32_t b = ctx.state[1];
    uint32_t c = ctx.state[2];
    uint32_t d = ctx.state[3];
    uint32_t e = ctx.state[4];
    uint32_t f = ctx.state[5];
    uint32_t g = ctx.state[6];
    uint32_t h = ctx.state[7];

    for (size_t i = 0; i < 64; ++i) {
        const uint32_t s1 = RotRight(e, 6u) ^ RotRight(e, 11u) ^ RotRight(e, 25u);
        const uint32_t ch = (e & f) ^ ((~e) & g);
        const uint32_t temp1 = h + s1 + ch + K_SHA256[i] + w[i];
        const uint32_t s0 = RotRight(a, 2u) ^ RotRight(a, 13u) ^ RotRight(a, 22u);
        const uint32_t maj = (a & b) ^ (a & c) ^ (b & c);
        const uint32_t temp2 = s0 + maj;

        h = g;
        g = f;
        f = e;
        e = d + temp1;
        d = c;
        c = b;
        b = a;
        a = temp1 + temp2;
    }

    ctx.state[0] += a;
    ctx.state[1] += b;
    ctx.state[2] += c;
    ctx.state[3] += d;
    ctx.state[4] += e;
    ctx.state[5] += f;
    ctx.state[6] += g;
    ctx.state[7] += h;
}

} // namespace

void CryptoHMAC::Sha256Init(Sha256Context& ctx) noexcept {
    ctx.state[0] = 0x6a09e667u;
    ctx.state[1] = 0xbb67ae85u;
    ctx.state[2] = 0x3c6ef372u;
    ctx.state[3] = 0xa54ff53au;
    ctx.state[4] = 0x510e527fu;
    ctx.state[5] = 0x9b05688cu;
    ctx.state[6] = 0x1f83d9abu;
    ctx.state[7] = 0x5be0cd19u;
    ctx.bitCount = 0;
    ctx.bufferLen = 0;
    std::memset(ctx.buffer, 0, sizeof(ctx.buffer));
}

void CryptoHMAC::Sha256Update(Sha256Context& ctx, const uint8_t* data, size_t len) noexcept {
    if (!data || len == 0) return;

    size_t offset = 0;
    if (ctx.bufferLen > 0) {
        const size_t space = SHA256_BLOCK_SIZE - ctx.bufferLen;
        const size_t toCopy = (len < space) ? len : space;
        std::memcpy(ctx.buffer + ctx.bufferLen, data, toCopy);
        ctx.bufferLen += toCopy;
        offset += toCopy;
        ctx.bitCount += static_cast<uint64_t>(toCopy) * 8ULL;

        if (ctx.bufferLen == SHA256_BLOCK_SIZE) {
            TransformBlock(ctx, ctx.buffer);
            ctx.bufferLen = 0;
        }
    }

    while (offset + SHA256_BLOCK_SIZE <= len) {
        TransformBlock(ctx, data + offset);
        offset += SHA256_BLOCK_SIZE;
        ctx.bitCount += static_cast<uint64_t>(SHA256_BLOCK_SIZE) * 8ULL;
    }

    if (offset < len) {
        const size_t rem = len - offset;
        std::memcpy(ctx.buffer, data + offset, rem);
        ctx.bufferLen = rem;
        ctx.bitCount += static_cast<uint64_t>(rem) * 8ULL;
    }
}

void CryptoHMAC::Sha256Final(Sha256Context& ctx, Digest& outDigest) noexcept {
    const uint64_t totalBits = ctx.bitCount;

    ctx.buffer[ctx.bufferLen++] = 0x80u;
    if (ctx.bufferLen > 56) {
        while (ctx.bufferLen < SHA256_BLOCK_SIZE) {
            ctx.buffer[ctx.bufferLen++] = 0x00u;
        }
        TransformBlock(ctx, ctx.buffer);
        ctx.bufferLen = 0;
    }

    while (ctx.bufferLen < 56) {
        ctx.buffer[ctx.bufferLen++] = 0x00u;
    }

    for (size_t i = 0; i < 8; ++i) {
        ctx.buffer[56 + i] = static_cast<uint8_t>((totalBits >> (56u - i * 8u)) & 0xFFu);
    }
    TransformBlock(ctx, ctx.buffer);

    for (size_t i = 0; i < 8; ++i) {
        outDigest[i * 4 + 0] = static_cast<uint8_t>((ctx.state[i] >> 24u) & 0xFFu);
        outDigest[i * 4 + 1] = static_cast<uint8_t>((ctx.state[i] >> 16u) & 0xFFu);
        outDigest[i * 4 + 2] = static_cast<uint8_t>((ctx.state[i] >>  8u) & 0xFFu);
        outDigest[i * 4 + 3] = static_cast<uint8_t>((ctx.state[i]       ) & 0xFFu);
    }

    std::memset(ctx.buffer, 0, sizeof(ctx.buffer));
    ctx.bufferLen = 0;
}

CryptoHMAC::Digest CryptoHMAC::ComputeSha256(const void* data, size_t len) noexcept {
    Sha256Context ctx{};
    Sha256Init(ctx);
    if (data && len > 0) {
        Sha256Update(ctx, static_cast<const uint8_t*>(data), len);
    }
    Digest digest{};
    Sha256Final(ctx, digest);
    return digest;
}

CryptoHMAC::Digest CryptoHMAC::ComputeHmacSha256(const void* key, size_t keyLen,
                                                 const void* data, size_t dataLen) noexcept {
    uint8_t keyBlock[SHA256_BLOCK_SIZE]{};
    if (key && keyLen > 0) {
        if (keyLen > SHA256_BLOCK_SIZE) {
            const Digest hashedKey = ComputeSha256(key, keyLen);
            std::memcpy(keyBlock, hashedKey.data(), SHA256_DIGEST_SIZE);
        } else {
            std::memcpy(keyBlock, key, keyLen);
        }
    }

    uint8_t oKeyPad[SHA256_BLOCK_SIZE]{};
    uint8_t iKeyPad[SHA256_BLOCK_SIZE]{};
    for (size_t i = 0; i < SHA256_BLOCK_SIZE; ++i) {
        oKeyPad[i] = keyBlock[i] ^ 0x5Cu;
        iKeyPad[i] = keyBlock[i] ^ 0x36u;
    }

    Sha256Context innerCtx{};
    Sha256Init(innerCtx);
    Sha256Update(innerCtx, iKeyPad, SHA256_BLOCK_SIZE);
    if (data && dataLen > 0) {
        Sha256Update(innerCtx, static_cast<const uint8_t*>(data), dataLen);
    }
    Digest innerDigest{};
    Sha256Final(innerCtx, innerDigest);

    Sha256Context outerCtx{};
    Sha256Init(outerCtx);
    Sha256Update(outerCtx, oKeyPad, SHA256_BLOCK_SIZE);
    Sha256Update(outerCtx, innerDigest.data(), SHA256_DIGEST_SIZE);

    Digest outMac{};
    Sha256Final(outerCtx, outMac);

    std::memset(keyBlock, 0, sizeof(keyBlock));
    std::memset(oKeyPad, 0, sizeof(oKeyPad));
    std::memset(iKeyPad, 0, sizeof(iKeyPad));
    return outMac;
}

bool CryptoHMAC::ConstantTimeEqual(const Digest& a, const Digest& b) noexcept {
    uint8_t diff = 0;
    for (size_t i = 0; i < SHA256_DIGEST_SIZE; ++i) {
        diff |= static_cast<uint8_t>(a[i] ^ b[i]);
    }
    return diff == 0;
}

} // namespace Neleo

#pragma once

#include "Net/NetTypes.hpp"
#include <cstddef>
#include <cstdint>
#include <string_view>

namespace Neleo {

/**
 * @brief [Jason Gregory Cap. 20.2] Escritor binario determinista en Network Byte Order (Big-Endian).
 * Totalmente independiente de la endianness de la CPU (PowerPC Big-Endian vs x86-64 Little-Endian)
 * y con cero asignaciones dinámicas en heap.
 */
class NetBitWriter {
public:
    NetBitWriter(uint8_t* buffer, size_t capacity) noexcept
        : m_buffer(buffer), m_capacity(capacity) {}

    bool WriteU8(uint8_t value) noexcept;
    bool WriteU16(uint16_t value) noexcept;
    bool WriteU32(uint32_t value) noexcept;
    bool WriteI16(int16_t value) noexcept;
    bool WriteI32(int32_t value) noexcept;
    bool WriteFloat(float value) noexcept;
    bool WriteQuantizedFloat16(float value, float minVal, float maxVal) noexcept;
    bool WriteBytes(const uint8_t* data, size_t length) noexcept;
    bool WriteString(std::string_view str) noexcept;
    bool WriteHeader(const NetPacketHeader& header) noexcept;

    [[nodiscard]] size_t GetBytesWritten() const noexcept { return m_offset; }
    [[nodiscard]] size_t GetBytesRemaining() const noexcept { return (m_offset <= m_capacity) ? (m_capacity - m_offset) : 0u; }
    [[nodiscard]] bool HasOverflowed() const noexcept { return m_overflowed; }
    [[nodiscard]] const uint8_t* GetData() const noexcept { return m_buffer; }

private:
    uint8_t* m_buffer{nullptr};
    size_t m_capacity{0};
    size_t m_offset{0};
    bool m_overflowed{false};
};

/**
 * @brief [Jason Gregory Cap. 20.2] Lector binario determinista en Network Byte Order (Big-Endian).
 */
class NetBitReader {
public:
    NetBitReader(const uint8_t* buffer, size_t size) noexcept
        : m_buffer(buffer), m_size(size) {}

    bool ReadU8(uint8_t& outValue) noexcept;
    bool ReadU16(uint16_t& outValue) noexcept;
    bool ReadU32(uint32_t& outValue) noexcept;
    bool ReadI16(int16_t& outValue) noexcept;
    bool ReadI32(int32_t& outValue) noexcept;
    bool ReadFloat(float& outValue) noexcept;
    bool ReadQuantizedFloat16(float minVal, float maxVal, float& outValue) noexcept;
    bool ReadBytes(uint8_t* outData, size_t length) noexcept;
    bool ReadString(char* outBuf, size_t outBufCapacity, size_t& outLen) noexcept;
    bool ReadHeader(NetPacketHeader& outHeader) noexcept;

    [[nodiscard]] size_t GetBytesRead() const noexcept { return m_offset; }
    [[nodiscard]] size_t GetBytesRemaining() const noexcept { return (m_offset <= m_size) ? (m_size - m_offset) : 0u; }
    [[nodiscard]] bool HasOverflowed() const noexcept { return m_overflowed; }

private:
    const uint8_t* m_buffer{nullptr};
    size_t m_size{0};
    size_t m_offset{0};
    bool m_overflowed{false};
};

} // namespace Neleo

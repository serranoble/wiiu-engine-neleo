#include "Net/NetBitStream.hpp"
#include <algorithm>
#include <bit>
#include <cmath>
#include <cstdio>
#include <cstring>

namespace Neleo {

bool NetAddress::ParseIPv4(std::string_view ipStr, uint16_t p, NetAddress& outAddr) noexcept {
    if (ipStr.empty()) {
        return false;
    }

    uint8_t parts[4]{0, 0, 0, 0};
    int partIdx = 0;
    uint32_t accum = 0;
    bool hasDigit = false;

    for (char c : ipStr) {
        if (c >= '0' && c <= '9') {
            accum = accum * 10u + static_cast<uint32_t>(c - '0');
            if (accum > 255u) {
                return false;
            }
            hasDigit = true;
        } else if (c == '.') {
            if (!hasDigit || partIdx >= 3) {
                return false;
            }
            parts[partIdx++] = static_cast<uint8_t>(accum);
            accum = 0;
            hasDigit = false;
        } else {
            return false;
        }
    }

    if (!hasDigit || partIdx != 3) {
        return false;
    }
    parts[3] = static_cast<uint8_t>(accum);

    outAddr = FromIPv4(parts[0], parts[1], parts[2], parts[3], p);
    return true;
}

void NetAddress::Format(char* outBuf, size_t bufSize) const noexcept {
    if (!outBuf || bufSize == 0) {
        return;
    }
    std::snprintf(outBuf, bufSize, "%u.%u.%u.%u:%u",
                  static_cast<unsigned>(octets[0]),
                  static_cast<unsigned>(octets[1]),
                  static_cast<unsigned>(octets[2]),
                  static_cast<unsigned>(octets[3]),
                  static_cast<unsigned>(port));
}

// =============================================================================
// NetBitWriter (Big-Endian / Network Byte Order)
// =============================================================================

bool NetBitWriter::WriteU8(uint8_t value) noexcept {
    if (!m_buffer || m_offset + 1u > m_capacity) {
        m_overflowed = true;
        return false;
    }
    m_buffer[m_offset++] = value;
    return true;
}

bool NetBitWriter::WriteU16(uint16_t value) noexcept {
    if (!m_buffer || m_offset + 2u > m_capacity) {
        m_overflowed = true;
        return false;
    }
    m_buffer[m_offset++] = static_cast<uint8_t>((value >> 8) & 0xFFu);
    m_buffer[m_offset++] = static_cast<uint8_t>(value & 0xFFu);
    return true;
}

bool NetBitWriter::WriteU32(uint32_t value) noexcept {
    if (!m_buffer || m_offset + 4u > m_capacity) {
        m_overflowed = true;
        return false;
    }
    m_buffer[m_offset++] = static_cast<uint8_t>((value >> 24) & 0xFFu);
    m_buffer[m_offset++] = static_cast<uint8_t>((value >> 16) & 0xFFu);
    m_buffer[m_offset++] = static_cast<uint8_t>((value >> 8) & 0xFFu);
    m_buffer[m_offset++] = static_cast<uint8_t>(value & 0xFFu);
    return true;
}

bool NetBitWriter::WriteI16(int16_t value) noexcept {
    return WriteU16(static_cast<uint16_t>(value));
}

bool NetBitWriter::WriteI32(int32_t value) noexcept {
    return WriteU32(static_cast<uint32_t>(value));
}

bool NetBitWriter::WriteFloat(float value) noexcept {
    static_assert(sizeof(float) == sizeof(uint32_t), "IEEE-754 float must be 32 bits");
    return WriteU32(std::bit_cast<uint32_t>(value));
}

bool NetBitWriter::WriteQuantizedFloat16(float value, float minVal, float maxVal) noexcept {
    if (!std::isfinite(value) || maxVal <= minVal) {
        return WriteU16(0u);
    }
    float normalized = std::clamp((value - minVal) / (maxVal - minVal), 0.0f, 1.0f);
    uint16_t quantized = static_cast<uint16_t>(std::round(normalized * 65535.0f));
    return WriteU16(quantized);
}

bool NetBitWriter::WriteBytes(const uint8_t* data, size_t length) noexcept {
    if (length == 0u) {
        return true;
    }
    if (!m_buffer || !data || length > GetBytesRemaining()) {
        m_overflowed = true;
        return false;
    }
    std::memcpy(m_buffer + m_offset, data, length);
    m_offset += length;
    return true;
}

bool NetBitWriter::WriteString(std::string_view str) noexcept {
    if (str.size() > 65535u) {
        m_overflowed = true;
        return false;
    }
    if (!WriteU16(static_cast<uint16_t>(str.size()))) {
        return false;
    }
    return WriteBytes(reinterpret_cast<const uint8_t*>(str.data()), str.size());
}

bool NetBitWriter::WriteHeader(const NetPacketHeader& header) noexcept {
    return WriteU32(header.magic) &&
           WriteU8(header.version) &&
           WriteU8(static_cast<uint8_t>(header.type)) &&
           WriteU16(header.sequence) &&
           WriteU16(header.ack) &&
           WriteU32(header.ackBitfield) &&
           WriteU16(header.tick);
}

// =============================================================================
// NetBitReader (Big-Endian / Network Byte Order)
// =============================================================================

bool NetBitReader::ReadU8(uint8_t& outValue) noexcept {
    if (!m_buffer || m_offset + 1u > m_size) {
        m_overflowed = true;
        return false;
    }
    outValue = m_buffer[m_offset++];
    return true;
}

bool NetBitReader::ReadU16(uint16_t& outValue) noexcept {
    if (!m_buffer || m_offset + 2u > m_size) {
        m_overflowed = true;
        return false;
    }
    outValue = static_cast<uint16_t>(
        (static_cast<uint16_t>(m_buffer[m_offset]) << 8) |
        static_cast<uint16_t>(m_buffer[m_offset + 1u])
    );
    m_offset += 2u;
    return true;
}

bool NetBitReader::ReadU32(uint32_t& outValue) noexcept {
    if (!m_buffer || m_offset + 4u > m_size) {
        m_overflowed = true;
        return false;
    }
    outValue = (static_cast<uint32_t>(m_buffer[m_offset]) << 24) |
               (static_cast<uint32_t>(m_buffer[m_offset + 1u]) << 16) |
               (static_cast<uint32_t>(m_buffer[m_offset + 2u]) << 8) |
               static_cast<uint32_t>(m_buffer[m_offset + 3u]);
    m_offset += 4u;
    return true;
}

bool NetBitReader::ReadI16(int16_t& outValue) noexcept {
    uint16_t raw = 0;
    if (!ReadU16(raw)) {
        return false;
    }
    outValue = static_cast<int16_t>(raw);
    return true;
}

bool NetBitReader::ReadI32(int32_t& outValue) noexcept {
    uint32_t raw = 0;
    if (!ReadU32(raw)) {
        return false;
    }
    outValue = static_cast<int32_t>(raw);
    return true;
}

bool NetBitReader::ReadFloat(float& outValue) noexcept {
    uint32_t raw = 0;
    if (!ReadU32(raw)) {
        return false;
    }
    outValue = std::bit_cast<float>(raw);
    return true;
}

bool NetBitReader::ReadQuantizedFloat16(float minVal, float maxVal, float& outValue) noexcept {
    uint16_t q = 0;
    if (!ReadU16(q)) {
        return false;
    }
    if (maxVal <= minVal) {
        outValue = minVal;
        return true;
    }
    const float normalized = static_cast<float>(q) / 65535.0f;
    outValue = minVal + normalized * (maxVal - minVal);
    return true;
}

bool NetBitReader::ReadBytes(uint8_t* outData, size_t length) noexcept {
    if (length == 0u) {
        return true;
    }
    if (!m_buffer || !outData || length > GetBytesRemaining()) {
        m_overflowed = true;
        return false;
    }
    std::memcpy(outData, m_buffer + m_offset, length);
    m_offset += length;
    return true;
}

bool NetBitReader::ReadString(char* outBuf, size_t outBufCapacity, size_t& outLen) noexcept {
    outLen = 0;
    uint16_t strLen = 0;
    if (!ReadU16(strLen)) {
        return false;
    }
    if (static_cast<size_t>(strLen) > GetBytesRemaining()) {
        m_overflowed = true;
        return false;
    }
    if (outBuf && outBufCapacity > 0u) {
        const size_t copyLen = std::min<size_t>(strLen, outBufCapacity - 1u);
        if (copyLen > 0u) {
            std::memcpy(outBuf, m_buffer + m_offset, copyLen);
        }
        outBuf[copyLen] = '\0';
        outLen = copyLen;
    }
    m_offset += strLen;
    return true;
}

bool NetBitReader::ReadHeader(NetPacketHeader& outHeader) noexcept {
    uint8_t typeRaw = 0;
    const bool ok = ReadU32(outHeader.magic) &&
                    ReadU8(outHeader.version) &&
                    ReadU8(typeRaw) &&
                    ReadU16(outHeader.sequence) &&
                    ReadU16(outHeader.ack) &&
                    ReadU32(outHeader.ackBitfield) &&
                    ReadU16(outHeader.tick);
    if (!ok) {
        return false;
    }
    if (outHeader.magic != NetProtocol::MAGIC ||
        outHeader.version != NetProtocol::VERSION ||
        typeRaw > static_cast<uint8_t>(NetMessageType::Disconnect)) {
        return false;
    }
    outHeader.type = static_cast<NetMessageType>(typeRaw);
    return true;
}

} // namespace Neleo

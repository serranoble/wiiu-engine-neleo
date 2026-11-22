#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string_view>

namespace WiiU {

/**
 * @brief [Jason Gregory Cap. 20: "Multiplayer & Networking"]
 * Tipos base, constantes de protocolo y métricas de red sin asignaciones dinámicas.
 */

struct NetProtocol {
    static constexpr uint32_t MAGIC = 0x57554E31u; // "WUN1"
    static constexpr uint8_t VERSION = 1u;
    static constexpr size_t HEADER_BYTES = 16u;
    static constexpr size_t MAX_PACKET_BYTES = 1200u; // Seguro bajo MTU IPv4 estándar (1500B)
    static constexpr size_t MAX_PAYLOAD_BYTES = MAX_PACKET_BYTES - HEADER_BYTES;
    static constexpr uint16_t DEFAULT_TCP_PORT = 7777u;
    static constexpr uint16_t DEFAULT_UDP_PORT = 7778u;
};

enum class NetResult : uint8_t {
    Ok = 0,
    WouldBlock = 1,
    Disconnected = 2,
    InvalidAddress = 3,
    NotInitialized = 4,
    BufferTooSmall = 5,
    SocketError = 6
};

enum class NetMessageType : uint8_t {
    Heartbeat = 0,
    Ping = 1,
    Pong = 2,
    ClientHello = 3,
    ServerWelcome = 4,
    InputCommand = 5,
    WorldSnapshot = 6,
    ControlText = 7,
    Disconnect = 8
};

/**
 * @brief Dirección IPv4 + puerto almacenada de forma portable sin heap ni cabeceras del OS.
 */
struct NetAddress {
    uint8_t octets[4]{0, 0, 0, 0};
    uint16_t port{0};

    [[nodiscard]] static constexpr NetAddress FromIPv4(uint8_t a, uint8_t b, uint8_t c, uint8_t d, uint16_t p) noexcept {
        return NetAddress{{a, b, c, d}, p};
    }

    [[nodiscard]] static constexpr NetAddress Loopback(uint16_t p) noexcept {
        return FromIPv4(127, 0, 0, 1, p);
    }

    [[nodiscard]] static bool ParseIPv4(std::string_view ipStr, uint16_t p, NetAddress& outAddr) noexcept;

    [[nodiscard]] constexpr uint32_t ToHostU32() const noexcept {
        return (static_cast<uint32_t>(octets[0]) << 24) |
               (static_cast<uint32_t>(octets[1]) << 16) |
               (static_cast<uint32_t>(octets[2]) << 8) |
               static_cast<uint32_t>(octets[3]);
    }

    [[nodiscard]] constexpr bool IsValid() const noexcept {
        return port != 0 && (octets[0] != 0 || octets[1] != 0 || octets[2] != 0 || octets[3] != 0);
    }

    [[nodiscard]] constexpr bool operator==(const NetAddress& other) const noexcept {
        return octets[0] == other.octets[0] &&
               octets[1] == other.octets[1] &&
               octets[2] == other.octets[2] &&
               octets[3] == other.octets[3] &&
               port == other.port;
    }

    [[nodiscard]] constexpr bool operator!=(const NetAddress& other) const noexcept {
        return !(*this == other);
    }

    void Format(char* outBuf, size_t bufSize) const noexcept;
};

/**
 * @brief Cabecera fija de 16 bytes en cable (Network Byte Order / Big-Endian).
 */
struct NetPacketHeader {
    uint32_t magic{NetProtocol::MAGIC};
    uint8_t version{NetProtocol::VERSION};
    NetMessageType type{NetMessageType::Heartbeat};
    uint16_t sequence{0};
    uint16_t ack{0};
    uint32_t ackBitfield{0};
    uint16_t tick{0};
};

/**
 * @brief Telemetría de enlace UDP/TCP para diagnóstico en tiempo real.
 */
struct NetStats {
    float rttMs{0.0f};
    float smoothedRttMs{0.0f};
    float jitterMs{0.0f};
    float packetLossPct{0.0f};
    uint32_t packetsSent{0};
    uint32_t packetsReceived{0};
    uint32_t packetsAcked{0};
    uint32_t packetsLost{0};
    uint64_t bytesSent{0};
    uint64_t bytesReceived{0};
};

using NetPacketBuffer = std::array<uint8_t, NetProtocol::MAX_PACKET_BYTES>;

/**
 * @brief Comparación modular de números de secuencia de 16 bits (maneja wrap-around en 65535 -> 0).
 */
[[nodiscard]] inline constexpr bool SequenceGreaterThan(uint16_t s1, uint16_t s2) noexcept {
    return ((s1 > s2) && (static_cast<uint16_t>(s1 - s2) <= 32768u)) ||
           ((s1 < s2) && (static_cast<uint16_t>(s2 - s1) > 32768u));
}

} // namespace WiiU

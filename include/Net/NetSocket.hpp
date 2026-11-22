#pragma once

#include "Net/NetTypes.hpp"
#include <cstddef>
#include <cstdint>

namespace WiiU {

/**
 * @brief [Jason Gregory Cap. 20.1] Capa de ciclo de vida del subsistema de red (HAL).
 * Encapsula Cafe OS nsysnet (Wii U) y Winsock2 (Windows PC) sin filtrar cabeceras del OS.
 */
class NetPlatform {
public:
    static bool Initialize() noexcept;
    static void Shutdown() noexcept;
    [[nodiscard]] static bool IsInitialized() noexcept;

    /**
     * @brief Autodiagnóstico determinista sin tráfico externo (BitStream, Endianness,
     * aritmética de secuencias, NetAddress::ParseIPv4 y SnapshotInterpolator).
     */
    [[nodiscard]] static bool VerifyNetIntegrity() noexcept;
};

/**
 * @brief Socket UDP IPv4 no bloqueante (move-only, Rule of 5, cero heap).
 */
class UdpSocket {
public:
    UdpSocket() noexcept = default;
    ~UdpSocket() noexcept;

    UdpSocket(const UdpSocket&) = delete;
    UdpSocket& operator=(const UdpSocket&) = delete;
    UdpSocket(UdpSocket&& other) noexcept;
    UdpSocket& operator=(UdpSocket&& other) noexcept;

    NetResult Open(uint16_t bindPort = 0) noexcept;
    void Close() noexcept;
    [[nodiscard]] bool IsOpen() const noexcept { return m_handle != INVALID_HANDLE; }
    [[nodiscard]] uint16_t GetBoundPort() const noexcept { return m_boundPort; }

    NetResult SendTo(const NetAddress& dest, const uint8_t* data, size_t size, size_t& outSent) noexcept;
    NetResult ReceiveFrom(NetAddress& outFrom, uint8_t* outBuf, size_t bufCap, size_t& outReceived) noexcept;

private:
    static constexpr intptr_t INVALID_HANDLE = -1;
    intptr_t m_handle{INVALID_HANDLE};
    uint16_t m_boundPort{0};
};

/**
 * @brief Socket TCP IPv4 no bloqueante (move-only, Rule of 5, cero heap).
 */
class TcpSocket {
public:
    TcpSocket() noexcept = default;
    ~TcpSocket() noexcept;

    TcpSocket(const TcpSocket&) = delete;
    TcpSocket& operator=(const TcpSocket&) = delete;
    TcpSocket(TcpSocket&& other) noexcept;
    TcpSocket& operator=(TcpSocket&& other) noexcept;

    NetResult ConnectNonBlocking(const NetAddress& remote) noexcept;
    NetResult PollConnect(bool& outConnected) noexcept;
    void Close() noexcept;

    [[nodiscard]] bool IsOpen() const noexcept { return m_handle != INVALID_HANDLE; }
    [[nodiscard]] bool IsConnected() const noexcept { return m_connected; }
    [[nodiscard]] bool IsConnecting() const noexcept { return m_connecting; }

    NetResult Send(const uint8_t* data, size_t size, size_t& outSent) noexcept;
    NetResult Receive(uint8_t* outBuf, size_t bufCap, size_t& outReceived) noexcept;

private:
    static constexpr intptr_t INVALID_HANDLE = -1;
    intptr_t m_handle{INVALID_HANDLE};
    bool m_connecting{false};
    bool m_connected{false};
};

} // namespace WiiU

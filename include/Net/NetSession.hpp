#pragma once

#include "Net/NetTypes.hpp"
#include "Net/NetSocket.hpp"
#include <array>
#include <cstddef>
#include <cstdint>
#include <string_view>

namespace WiiU {

/**
 * @brief Callbacks desacoplados sin asignaciones dinámicas (DIP).
 */
using UdpPacketCallback = void(*)(const NetPacketHeader& header, const uint8_t* payload, size_t payloadSize, void* userData);
using TcpMessageCallback = void(*)(const uint8_t* payload, size_t payloadSize, void* userData);

enum class NetSessionState : uint8_t {
    Disconnected = 0,
    Connecting = 1,
    Connected = 2,
    TimedOut = 3
};

/**
 * @brief [Jason Gregory Cap. 20.1 & 20.3] Sesión Cliente Dual (UDP Tiempo Real + TCP Control).
 *
 * - Canal UDP: secuencias de 16 bits, ventana ACK deslizante de 32 bits, cálculo en vivo
 *   de RTT, jitter, packet loss % y heartbeats automáticos sin alocaciones dinámicas.
 * - Canal TCP: conexión no bloqueante con framing [uint16_t length][payload] sobre buffer fijo.
 */
class NetSession {
public:
    static constexpr size_t SENT_WINDOW_SIZE = 64u;
    static constexpr size_t TCP_BUFFER_CAPACITY = 2048u;
    static constexpr float HEARTBEAT_INTERVAL_SEC = 0.5f;
    static constexpr float CONNECTION_TIMEOUT_SEC = 5.0f;

    NetSession() noexcept = default;
    ~NetSession() noexcept { Disconnect(); }

    NetSession(const NetSession&) = delete;
    NetSession& operator=(const NetSession&) = delete;
    NetSession(NetSession&&) = delete;
    NetSession& operator=(NetSession&&) = delete;

    /**
     * @brief Inicia la sesión contra el servidor remoto (abre UDP local y conecta TCP no bloqueante).
     */
    bool Connect(const NetAddress& udpRemote, const NetAddress& tcpRemote, uint16_t localUdpPort = 0) noexcept;
    void Disconnect() noexcept;

    /**
     * @brief Bombea recepción/envío UDP y TCP, actualiza RTT/loss y emite heartbeats (0 heap allocs).
     */
    void Update(float dt) noexcept;

    /**
     * @brief Envía un paquete UDP con cabecera de protocolo y seguimiento de secuencia/ACK.
     */
    NetResult SendUdpPacket(NetMessageType type, const uint8_t* payload, size_t payloadSize, uint16_t tick = 0) noexcept;

    /**
     * @brief Envía un mensaje de control por TCP con prefijo de longitud de 2 bytes (Network Byte Order).
     */
    NetResult SendTcpMessage(const uint8_t* payload, size_t payloadSize) noexcept;
    NetResult SendTcpText(std::string_view text) noexcept;

    void SetUdpCallback(UdpPacketCallback cb, void* userData) noexcept {
        m_udpCallback = cb;
        m_udpUserData = userData;
    }

    void SetTcpCallback(TcpMessageCallback cb, void* userData) noexcept {
        m_tcpCallback = cb;
        m_tcpUserData = userData;
    }

    [[nodiscard]] NetSessionState GetState() const noexcept { return m_state; }
    [[nodiscard]] bool IsTcpConnected() const noexcept { return m_tcpSocket.IsConnected(); }
    [[nodiscard]] bool IsUdpOpen() const noexcept { return m_udpSocket.IsOpen(); }
    [[nodiscard]] const NetStats& GetStats() const noexcept { return m_stats; }
    [[nodiscard]] uint16_t GetLocalSequence() const noexcept { return m_localSequence; }
    [[nodiscard]] uint16_t GetRemoteSequence() const noexcept { return m_remoteSequence; }

private:
    struct SentPacketRecord {
        uint16_t sequence{0};
        double sentTimeSec{0.0};
        bool active{false};
        bool acked{false};
    };

    void PollUdpReceive() noexcept;
    void PollTcp() noexcept;
    void FlushTcpTx() noexcept;
    void ProcessAck(uint16_t ackSeq, uint32_t ackBits) noexcept;
    void RecordAckedPacket(SentPacketRecord& rec) noexcept;
    void UpdateRemoteAckState(uint16_t incomingSeq) noexcept;
    void EvaluateLostPackets() noexcept;

    UdpSocket m_udpSocket;
    TcpSocket m_tcpSocket;
    NetAddress m_udpRemote{};
    NetAddress m_tcpRemote{};
    NetSessionState m_state{NetSessionState::Disconnected};

    double m_sessionTimeSec{0.0};
    double m_lastReceiveTimeSec{0.0};
    float m_heartbeatTimer{0.0f};

    uint16_t m_localSequence{0};
    uint16_t m_remoteSequence{0};
    uint32_t m_remoteAckBitfield{0};
    bool m_hasRemoteSequence{false};

    std::array<SentPacketRecord, SENT_WINDOW_SIZE> m_sentWindow{};
    NetPacketBuffer m_rxScratchBuffer{};
    NetPacketBuffer m_txScratchBuffer{};

    std::array<uint8_t, TCP_BUFFER_CAPACITY> m_tcpRxBuffer{};
    size_t m_tcpRxSize{0};
    std::array<uint8_t, TCP_BUFFER_CAPACITY> m_tcpTxBuffer{};
    size_t m_tcpTxSize{0};

    NetStats m_stats{};
    UdpPacketCallback m_udpCallback{nullptr};
    void* m_udpUserData{nullptr};
    TcpMessageCallback m_tcpCallback{nullptr};
    void* m_tcpUserData{nullptr};
};

} // namespace WiiU

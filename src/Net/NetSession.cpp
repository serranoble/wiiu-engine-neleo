#include "Net/NetSession.hpp"
#include "Net/NetBitStream.hpp"
#include <algorithm>
#include <cmath>
#include <cstring>

namespace WiiU {

bool NetSession::Connect(const NetAddress& udpRemote, const NetAddress& tcpRemote, uint16_t localUdpPort) noexcept {
    Disconnect();

    if (!NetPlatform::IsInitialized() || !udpRemote.IsValid()) {
        return false;
    }

    if (m_udpSocket.Open(localUdpPort) != NetResult::Ok) {
        return false;
    }

    m_udpRemote = udpRemote;
    m_tcpRemote = tcpRemote;
    m_sessionTimeSec = 0.0;
    m_lastReceiveTimeSec = 0.0;
    m_heartbeatTimer = 0.0f;
    m_localSequence = 0;
    m_remoteSequence = 0;
    m_remoteAckBitfield = 0;
    m_hasRemoteSequence = false;
    m_tcpRxSize = 0;
    m_tcpTxSize = 0;
    m_stats = NetStats{};
    for (auto& rec : m_sentWindow) {
        rec = SentPacketRecord{};
    }

    if (tcpRemote.IsValid()) {
        (void)m_tcpSocket.ConnectNonBlocking(tcpRemote);
    }

    m_state = NetSessionState::Connecting;
    (void)SendUdpPacket(NetMessageType::ClientHello, nullptr, 0, 0);
    return true;
}

void NetSession::Disconnect() noexcept {
    if (m_state == NetSessionState::Connected || m_state == NetSessionState::Connecting) {
        (void)SendUdpPacket(NetMessageType::Disconnect, nullptr, 0, 0);
    }
    m_udpSocket.Close();
    m_tcpSocket.Close();
    m_state = NetSessionState::Disconnected;
    m_tcpRxSize = 0;
    m_tcpTxSize = 0;
}

void NetSession::Update(float dt) noexcept {
    if (m_state == NetSessionState::Disconnected || m_state == NetSessionState::TimedOut) {
        return;
    }

    m_sessionTimeSec += static_cast<double>(dt);
    m_heartbeatTimer += dt;

    PollUdpReceive();
    PollTcp();
    EvaluateLostPackets();

    if (m_heartbeatTimer >= HEARTBEAT_INTERVAL_SEC) {
        m_heartbeatTimer = 0.0f;
        const NetMessageType pulseType = (m_state == NetSessionState::Connecting)
            ? NetMessageType::ClientHello
            : NetMessageType::Ping;
        (void)SendUdpPacket(pulseType, nullptr, 0, 0);
    }

    if ((m_state == NetSessionState::Connected || m_state == NetSessionState::Connecting) &&
        (m_sessionTimeSec - m_lastReceiveTimeSec) > static_cast<double>(CONNECTION_TIMEOUT_SEC)) {
        m_state = NetSessionState::TimedOut;
    }
}

NetResult NetSession::SendUdpPacket(NetMessageType type, const uint8_t* payload, size_t payloadSize, uint16_t tick) noexcept {
    if (!m_udpSocket.IsOpen()) {
        return NetResult::NotInitialized;
    }
    if (payloadSize > NetProtocol::MAX_PAYLOAD_BYTES || (!payload && payloadSize > 0u)) {
        return NetResult::BufferTooSmall;
    }

    // Reservar 0 como centinela de "sin ACK" saltando 0 en el wrap-around 65535 -> 1
    if (++m_localSequence == 0u) {
        m_localSequence = 1u;
    }
    const uint16_t seq = m_localSequence;

    NetPacketHeader hdr{};
    hdr.magic = NetProtocol::MAGIC;
    hdr.version = NetProtocol::VERSION;
    hdr.type = type;
    hdr.sequence = seq;
    hdr.ack = m_remoteSequence;
    hdr.ackBitfield = m_remoteAckBitfield;
    hdr.tick = tick;

    NetBitWriter writer(m_txScratchBuffer.data(), m_txScratchBuffer.size());
    if (!writer.WriteHeader(hdr)) {
        return NetResult::BufferTooSmall;
    }
    if (payloadSize > 0u && !writer.WriteBytes(payload, payloadSize)) {
        return NetResult::BufferTooSmall;
    }

    size_t sent = 0;
    const NetResult res = m_udpSocket.SendTo(m_udpRemote, writer.GetData(), writer.GetBytesWritten(), sent);
    if (res == NetResult::Ok) {
        m_stats.packetsSent++;
        m_stats.bytesSent += sent;

        auto& slot = m_sentWindow[seq % SENT_WINDOW_SIZE];
        if (slot.active && !slot.acked) {
            // El paquete previo en esta ranura circular expiró sin confirmación
            m_stats.packetsLost++;
        }
        slot.sequence = seq;
        slot.sentTimeSec = m_sessionTimeSec;
        slot.active = true;
        slot.acked = false;
    }
    return res;
}

NetResult NetSession::SendTcpMessage(const uint8_t* payload, size_t payloadSize) noexcept {
    if (!m_tcpSocket.IsConnected()) {
        return NetResult::Disconnected;
    }
    if ((!payload && payloadSize > 0u) || payloadSize > NetProtocol::MAX_PAYLOAD_BYTES) {
        return NetResult::BufferTooSmall;
    }

    const size_t frameSize = 2u + payloadSize;
    if (frameSize > TCP_BUFFER_CAPACITY - m_tcpTxSize) {
        return NetResult::BufferTooSmall;
    }

    // Encolar frame completo [uint16_t BE len][payload] en cola TX fija para evitar fragmentación corrupta
    m_tcpTxBuffer[m_tcpTxSize++] = static_cast<uint8_t>((payloadSize >> 8) & 0xFFu);
    m_tcpTxBuffer[m_tcpTxSize++] = static_cast<uint8_t>(payloadSize & 0xFFu);
    if (payloadSize > 0u) {
        std::memcpy(m_tcpTxBuffer.data() + m_tcpTxSize, payload, payloadSize);
        m_tcpTxSize += payloadSize;
    }

    FlushTcpTx();
    return m_tcpSocket.IsConnected() ? NetResult::Ok : NetResult::Disconnected;
}

NetResult NetSession::SendTcpText(std::string_view text) noexcept {
    return SendTcpMessage(reinterpret_cast<const uint8_t*>(text.data()), text.size());
}

void NetSession::FlushTcpTx() noexcept {
    while (m_tcpTxSize > 0u && m_tcpSocket.IsConnected()) {
        size_t sent = 0;
        const NetResult res = m_tcpSocket.Send(m_tcpTxBuffer.data(), m_tcpTxSize, sent);
        if (res == NetResult::Ok && sent > 0u) {
            m_stats.bytesSent += sent;
            const size_t remaining = m_tcpTxSize - sent;
            if (remaining > 0u) {
                std::memmove(m_tcpTxBuffer.data(), m_tcpTxBuffer.data() + sent, remaining);
            }
            m_tcpTxSize = remaining;
        } else {
            // WouldBlock o error (TcpSocket::Send ya cerró ante error fatal)
            break;
        }
    }
}

void NetSession::PollUdpReceive() noexcept {
    constexpr int MAX_PACKETS_PER_FRAME = 32;
    for (int i = 0; i < MAX_PACKETS_PER_FRAME; ++i) {
        NetAddress from{};
        size_t recvd = 0;
        const NetResult res = m_udpSocket.ReceiveFrom(from, m_rxScratchBuffer.data(), m_rxScratchBuffer.size(), recvd);
        if (res != NetResult::Ok || recvd < NetProtocol::HEADER_BYTES) {
            break;
        }
        // Filtrar datagramas LAN espurios que no provengan del servidor activo
        if (from != m_udpRemote) {
            continue;
        }

        NetBitReader reader(m_rxScratchBuffer.data(), recvd);
        NetPacketHeader hdr{};
        if (!reader.ReadHeader(hdr)) {
            continue;
        }

        m_stats.packetsReceived++;
        m_stats.bytesReceived += recvd;
        m_lastReceiveTimeSec = m_sessionTimeSec;
        if (m_state == NetSessionState::Connecting) {
            m_state = NetSessionState::Connected;
        }

        UpdateRemoteAckState(hdr.sequence);
        ProcessAck(hdr.ack, hdr.ackBitfield);

        if (m_udpCallback) {
            const uint8_t* payloadPtr = m_rxScratchBuffer.data() + NetProtocol::HEADER_BYTES;
            const size_t payloadLen = recvd - NetProtocol::HEADER_BYTES;
            m_udpCallback(hdr, payloadPtr, payloadLen, m_udpUserData);
        }
    }
}

void NetSession::PollTcp() noexcept {
    if (!m_tcpSocket.IsOpen()) {
        return;
    }

    if (m_tcpSocket.IsConnecting()) {
        bool connected = false;
        (void)m_tcpSocket.PollConnect(connected);
        if (!connected) {
            return;
        }
        (void)SendTcpText("HELLO ENGINE_NET_V1");
    }

    if (!m_tcpSocket.IsConnected()) {
        return;
    }

    FlushTcpTx();
    if (!m_tcpSocket.IsConnected()) {
        return;
    }

    const size_t freeSpace = TCP_BUFFER_CAPACITY - m_tcpRxSize;
    if (freeSpace > 0u) {
        size_t recvd = 0;
        const NetResult res = m_tcpSocket.Receive(m_tcpRxBuffer.data() + m_tcpRxSize, freeSpace, recvd);
        if (res == NetResult::Ok && recvd > 0u) {
            m_tcpRxSize += recvd;
            m_stats.bytesReceived += recvd;
        }
    }

    // Procesar frames completos [uint16_t length][payload]
    while (m_tcpRxSize >= 2u) {
        const uint16_t msgLen = static_cast<uint16_t>(
            (static_cast<uint16_t>(m_tcpRxBuffer[0]) << 8) |
            static_cast<uint16_t>(m_tcpRxBuffer[1])
        );
        if (msgLen > TCP_BUFFER_CAPACITY - 2u) {
            m_tcpRxSize = 0;
            break;
        }
        const size_t totalFrame = 2u + static_cast<size_t>(msgLen);
        if (m_tcpRxSize < totalFrame) {
            break;
        }

        if (m_tcpCallback) {
            m_tcpCallback(m_tcpRxBuffer.data() + 2u, msgLen, m_tcpUserData);
        }

        const size_t remaining = m_tcpRxSize - totalFrame;
        if (remaining > 0u) {
            std::memmove(m_tcpRxBuffer.data(), m_tcpRxBuffer.data() + totalFrame, remaining);
        }
        m_tcpRxSize = remaining;
    }
}

void NetSession::UpdateRemoteAckState(uint16_t incomingSeq) noexcept {
    if (incomingSeq == 0u) {
        return;
    }
    if (!m_hasRemoteSequence) {
        m_remoteSequence = incomingSeq;
        m_remoteAckBitfield = 0u;
        m_hasRemoteSequence = true;
        return;
    }

    if (SequenceGreaterThan(incomingSeq, m_remoteSequence)) {
        const uint16_t diff = static_cast<uint16_t>(incomingSeq - m_remoteSequence);
        if (diff < 32u) {
            m_remoteAckBitfield = (m_remoteAckBitfield << diff) | (1u << (diff - 1u));
        } else if (diff == 32u) {
            m_remoteAckBitfield = (1u << 31u);
        } else {
            m_remoteAckBitfield = 0u;
        }
        m_remoteSequence = incomingSeq;
    } else if (incomingSeq != m_remoteSequence) {
        const uint16_t diff = static_cast<uint16_t>(m_remoteSequence - incomingSeq);
        if (diff >= 1u && diff <= 32u) {
            m_remoteAckBitfield |= (1u << (diff - 1u));
        }
    }
}

void NetSession::ProcessAck(uint16_t ackSeq, uint32_t ackBits) noexcept {
    if (ackSeq == 0u) {
        return;
    }

    auto checkSeq = [this](uint16_t seq) noexcept {
        if (seq == 0u) {
            return;
        }
        auto& rec = m_sentWindow[seq % SENT_WINDOW_SIZE];
        if (rec.active && !rec.acked && rec.sequence == seq) {
            RecordAckedPacket(rec);
        }
    };

    checkSeq(ackSeq);
    for (uint32_t bit = 0; bit < 32u; ++bit) {
        if ((ackBits & (1u << bit)) != 0u) {
            uint16_t olderSeq = static_cast<uint16_t>(ackSeq - static_cast<uint16_t>(bit + 1u));
            if (olderSeq == 0u) {
                olderSeq = 65535u;
            }
            checkSeq(olderSeq);
        }
    }
}

void NetSession::RecordAckedPacket(SentPacketRecord& rec) noexcept {
    rec.acked = true;
    m_stats.packetsAcked++;

    const float sampleRttMs = static_cast<float>((m_sessionTimeSec - rec.sentTimeSec) * 1000.0);
    if (sampleRttMs >= 0.0f && sampleRttMs < 10000.0f) {
        const float delta = std::abs(sampleRttMs - m_stats.rttMs);
        m_stats.rttMs = sampleRttMs;
        if (m_stats.smoothedRttMs <= 0.001f) {
            m_stats.smoothedRttMs = sampleRttMs;
        } else {
            m_stats.smoothedRttMs += (sampleRttMs - m_stats.smoothedRttMs) * 0.125f;
        }
        m_stats.jitterMs += (delta - m_stats.jitterMs) * 0.125f;
    }
}

void NetSession::EvaluateLostPackets() noexcept {
    constexpr double LOSS_TIMEOUT_SEC = 1.0;
    for (auto& rec : m_sentWindow) {
        if (rec.active && !rec.acked && (m_sessionTimeSec - rec.sentTimeSec) > LOSS_TIMEOUT_SEC) {
            rec.active = false;
            m_stats.packetsLost++;
        }
    }
    const uint32_t resolved = m_stats.packetsAcked + m_stats.packetsLost;
    if (resolved > 0u) {
        m_stats.packetLossPct = (static_cast<float>(m_stats.packetsLost) * 100.0f) / static_cast<float>(resolved);
    }
}

} // namespace WiiU

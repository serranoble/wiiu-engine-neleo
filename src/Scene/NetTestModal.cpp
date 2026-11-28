#include "Scene/NetTestModal.hpp"
#include "Scene/SceneManager.hpp"
#include "Core/Config/EngineConfig.hpp"
#include "Core/Localization/LocalizationManager.hpp"
#include "Input/InputManager.hpp"
#include "Input/SoftwareKeyboard.hpp"
#include "Audio/AudioManager.hpp"
#include "Graphics/Render2D.hpp"
#include <algorithm>
#include <cstdio>
#include <cstring>

namespace Neleo {

NetTestModal::NetTestModal()
    : m_btnEditIp(125, 132, 620, 44, "", 2),
      m_btnUseCemu(760, 132, 395, 44, NELEO_LOC("net.btn.cemu"), 2),
      m_btnConnect(125, 190, 320, 46, NELEO_LOC("net.btn.connect"), 2),
      m_btnTcpPing(460, 190, 240, 46, NELEO_LOC("net.btn.tcp_ping"), 2),
      m_btnUdpCmd(715, 190, 240, 46, NELEO_LOC("net.btn.udp_cmd"), 2),
      m_btnClose(970, 190, 185, 46, NELEO_LOC("net.btn.close"), 2) {
    const auto& netCfg = EngineConfig::Instance().GetNetwork();
    std::strncpy(m_serverIp, netCfg.serverIp, sizeof(m_serverIp) - 1);
    m_serverIp[sizeof(m_serverIp) - 1] = '\0';
    std::strncpy(m_lastTcpMessage, NELEO_LOC_CSTR("net.no_tcp_msg"), sizeof(m_lastTcpMessage) - 1);
    m_lastTcpMessage[sizeof(m_lastTcpMessage) - 1] = '\0';
    m_tcpPort = netCfg.tcpPort;
    m_udpPort = netCfg.udpPort;

    m_btnEditIp.SetColors(
        ColorRGBA{18, 36, 62, 235}, ColorRGBA{35, 85, 145, 245},
        ColorRGBA::Cyan(), ColorRGBA::Yellow(), ColorRGBA::White()
    );
    m_btnUseCemu.SetColors(
        ColorRGBA{20, 40, 55, 235}, ColorRGBA{40, 95, 130, 245},
        ColorRGBA{100, 200, 255, 255}, ColorRGBA::Yellow(), ColorRGBA::White()
    );
    m_btnConnect.SetColors(
        ColorRGBA{15, 50, 30, 235}, ColorRGBA{0, 195, 115, 245},
        ColorRGBA::Green(), ColorRGBA::Yellow(), ColorRGBA::White()
    );
    m_btnTcpPing.SetColors(
        ColorRGBA{35, 30, 65, 235}, ColorRGBA{95, 75, 180, 245},
        ColorRGBA{160, 140, 255, 255}, ColorRGBA::Yellow(), ColorRGBA::White()
    );
    m_btnUdpCmd.SetColors(
        ColorRGBA{50, 35, 15, 235}, ColorRGBA{210, 135, 25, 245},
        ColorRGBA::Orange(), ColorRGBA::Yellow(), ColorRGBA::White()
    );
    m_btnClose.SetColors(
        ColorRGBA{55, 20, 25, 235}, ColorRGBA{195, 45, 55, 245},
        ColorRGBA::Red(), ColorRGBA::Yellow(), ColorRGBA::White()
    );

    RefreshButtonLabels();
}

NetTestModal::~NetTestModal() {
    m_session.SetUdpCallback(nullptr, nullptr);
    m_session.SetTcpCallback(nullptr, nullptr);
    m_session.Disconnect();
}

void NetTestModal::OnAttach() {
    m_session.SetUdpCallback(&NetTestModal::OnUdpPacketStatic, this);
    m_session.SetTcpCallback(&NetTestModal::OnTcpMessageStatic, this);
    RefreshButtonLabels();
}

void NetTestModal::OnDetach() {
    m_session.SetUdpCallback(nullptr, nullptr);
    m_session.SetTcpCallback(nullptr, nullptr);
    m_session.Disconnect();
}

void NetTestModal::OnUdpPacketStatic(const NetPacketHeader& header, const uint8_t* payload, size_t payloadSize, void* userData) {
    if (auto* self = static_cast<NetTestModal*>(userData)) {
        self->HandleUdpPacket(header, payload, payloadSize);
    }
}

void NetTestModal::OnTcpMessageStatic(const uint8_t* payload, size_t payloadSize, void* userData) {
    if (auto* self = static_cast<NetTestModal*>(userData)) {
        self->HandleTcpMessage(payload, payloadSize);
    }
}

void NetTestModal::HandleUdpPacket(const NetPacketHeader& header, const uint8_t* payload, size_t payloadSize) noexcept {
    if (header.type == NetMessageType::WorldSnapshot && payload && payloadSize > 0u) {
        NetBitReader reader(payload, payloadSize);
        NetTransformSnapshot snap{};
        if (DeserializeSnapshot(reader, snap)) {
            if (!m_hasServerTimeSync) {
                m_serverTimeOffsetSec = snap.timestampSec - m_localClockSec;
                m_hasServerTimeSync = true;
            }
            m_lastRawSnapshot = snap;
            m_hasRemoteSnapshot = true;
            m_snapshotsRx++;
            m_interpolator.PushSnapshot(snap);
        }
    }
}

void NetTestModal::HandleTcpMessage(const uint8_t* payload, size_t payloadSize) noexcept {
    if (!payload || payloadSize == 0u) return;
    const size_t copyLen = std::min<size_t>(payloadSize, sizeof(m_lastTcpMessage) - 1u);
    for (size_t i = 0u; i < copyLen; ++i) {
        const uint8_t c = payload[i];
        m_lastTcpMessage[i] = (c >= 0x20u && c <= 0x7Eu) ? static_cast<char>(c) : ' ';
    }
    m_lastTcpMessage[copyLen] = '\0';
}

void NetTestModal::RefreshButtonLabels() {
    char buf[96];
    std::snprintf(buf, sizeof(buf), NELEO_LOC_CSTR("net.btn.edit_ip"), m_serverIp);
    m_btnEditIp.SetLabel(buf);

    const bool active = (m_session.GetState() == NetSessionState::Connected ||
                         m_session.GetState() == NetSessionState::Connecting);
    m_btnConnect.SetLabel(active ? NELEO_LOC("net.btn.disconnect") : NELEO_LOC("net.btn.connect"));
}

void NetTestModal::SetFeedback(const char* msg, ColorRGBA color, float durationSec) noexcept {
    m_feedbackText = msg;
    m_feedbackColor = color;
    m_feedbackTimer = durationSec;
}

void NetTestModal::OpenIpKeyboard() {
    AudioManager::Instance().PlayClick();
    char inputBuf[48]{};
    if (SoftwareKeyboard::Show(NELEO_LOC_CSTR("net.kb.prompt"),
                               m_serverIp, inputBuf, sizeof(inputBuf),
                               KeyboardInputMode::IPv4, 15)) {
        NetAddress testAddr{};
        if (NetAddress::ParseIPv4(inputBuf, m_udpPort, testAddr) && testAddr.IsValid()) {
            std::strncpy(m_serverIp, inputBuf, sizeof(m_serverIp) - 1);
            m_serverIp[sizeof(m_serverIp) - 1] = '\0';

            auto& cfg = EngineConfig::Instance();
            std::strncpy(cfg.network.serverIp, m_serverIp, sizeof(cfg.network.serverIp) - 1);
            cfg.network.serverIp[sizeof(cfg.network.serverIp) - 1] = '\0';
            (void)cfg.SaveToFile();

            RefreshButtonLabels();
            SetFeedback(NELEO_LOC_CSTR("net.fb.ip_saved"), ColorRGBA::Green());
        } else {
            SetFeedback(NELEO_LOC_CSTR("net.fb.ip_invalid"), ColorRGBA::Red(), 3.5f);
        }
    } else {
        SetFeedback(NELEO_LOC_CSTR("net.fb.kb_cancelled"), ColorRGBA::Yellow(), 1.8f);
    }
}

void NetTestModal::SetLoopbackIp() {
    AudioManager::Instance().PlayClick();
    std::strncpy(m_serverIp, "127.0.0.1", sizeof(m_serverIp) - 1);
    m_serverIp[sizeof(m_serverIp) - 1] = '\0';
    RefreshButtonLabels();
    SetFeedback(NELEO_LOC_CSTR("net.fb.loopback"), ColorRGBA::Cyan());
}

void NetTestModal::ToggleConnection() {
    AudioManager::Instance().PlayClick();
    const bool active = (m_session.GetState() == NetSessionState::Connected ||
                         m_session.GetState() == NetSessionState::Connecting);
    if (active) {
        m_session.Disconnect();
        m_hasRemoteSnapshot = false;
        m_hasServerTimeSync = false;
        m_interpolator.Reset();
        RefreshButtonLabels();
        SetFeedback(NELEO_LOC_CSTR("net.fb.disconnected"), ColorRGBA::Yellow());
        return;
    }

    NetAddress udpAddr{};
    NetAddress tcpAddr{};
    if (!NetAddress::ParseIPv4(m_serverIp, m_udpPort, udpAddr) || !udpAddr.IsValid() ||
        !NetAddress::ParseIPv4(m_serverIp, m_tcpPort, tcpAddr) || !tcpAddr.IsValid()) {
        SetFeedback(NELEO_LOC_CSTR("net.fb.ip_invalid_conn"), ColorRGBA::Red(), 3.0f);
        return;
    }

    m_interpolator.Reset();
    m_hasRemoteSnapshot = false;
    m_hasServerTimeSync = false;
    m_snapshotsRx = 0;
    std::strncpy(m_lastTcpMessage, NELEO_LOC_CSTR("net.state.connecting"), sizeof(m_lastTcpMessage) - 1);
    m_lastTcpMessage[sizeof(m_lastTcpMessage) - 1] = '\0';

    if (m_session.Connect(udpAddr, tcpAddr, 0u)) {
        RefreshButtonLabels();
        SetFeedback(NELEO_LOC_CSTR("net.fb.connecting"), ColorRGBA::Cyan());
    } else {
        SetFeedback(NELEO_LOC_CSTR("net.fb.socket_err"), ColorRGBA::Red(), 3.0f);
    }
}

void NetTestModal::SendTcpPing() {
    AudioManager::Instance().PlayClick();
    if (!m_session.IsTcpConnected()) {
        SetFeedback(NELEO_LOC_CSTR("net.fb.tcp_not_conn"), ColorRGBA::Red());
        return;
    }
    char msg[48];
    const int n = std::snprintf(msg, sizeof(msg), "PING_TCP #%u", ++m_tcpPingCounter);
    if (n > 0) {
        const size_t len = std::min<size_t>(static_cast<size_t>(n), sizeof(msg) - 1u);
        if (m_session.SendTcpText(std::string_view(msg, len)) == NetResult::Ok) {
            SetFeedback(NELEO_LOC_CSTR("net.fb.tcp_sent"), ColorRGBA::Green(), 1.5f);
        } else {
            SetFeedback(NELEO_LOC_CSTR("net.fb.tcp_err"), ColorRGBA::Red());
        }
    }
}

void NetTestModal::SendUdpCommand() {
    AudioManager::Instance().PlayClick();
    if (!m_session.IsUdpOpen()) {
        SetFeedback(NELEO_LOC_CSTR("net.fb.udp_closed"), ColorRGBA::Red());
        return;
    }
    uint8_t payload[12]{};
    NetBitWriter writer(payload, sizeof(payload));
    (void)writer.WriteU32(++m_udpCmdCounter);
    (void)writer.WriteFloat(static_cast<float>(m_localClockSec));
    if (m_session.SendUdpPacket(NetMessageType::InputCommand, writer.GetData(), writer.GetBytesWritten(),
                                static_cast<uint16_t>(m_udpCmdCounter & 0xFFFFu)) == NetResult::Ok) {
        SetFeedback(NELEO_LOC_CSTR("net.fb.udp_sent"), ColorRGBA::Orange(), 1.5f);
    } else {
        SetFeedback(NELEO_LOC_CSTR("net.fb.udp_err"), ColorRGBA::Red());
    }
}

void NetTestModal::Update(float dt) {
    m_localClockSec += static_cast<double>(dt);
    if (m_feedbackTimer > 0.0f) {
        m_feedbackTimer -= dt;
    }

    const NetSessionState prevState = m_session.GetState();
    m_session.Update(dt);
    if (m_session.GetState() != prevState) {
        RefreshButtonLabels();
        if (m_session.GetState() == NetSessionState::Connected) {
            SetFeedback(NELEO_LOC_CSTR("net.fb.udp_active"), ColorRGBA::Green());
        } else if (m_session.GetState() == NetSessionState::TimedOut) {
            SetFeedback(NELEO_LOC_CSTR("net.fb.timeout"), ColorRGBA::Red(), 3.5f);
        }
    }

    const auto& inputMgr = InputManager::Instance();
    const auto& touch = inputMgr.GetTouchState();

    // Botones táctiles
    if (m_btnClose.Update(touch)) {
        AudioManager::Instance().PlayClick();
        SceneManager::Instance().GetModalStack().Pop();
        return;
    }
    if (m_btnEditIp.Update(touch)) {
        OpenIpKeyboard();
        return;
    }
    if (m_btnUseCemu.Update(touch)) {
        SetLoopbackIp();
        return;
    }
    if (m_btnConnect.Update(touch)) {
        ToggleConnection();
        return;
    }
    if (m_btnTcpPing.Update(touch)) {
        SendTcpPing();
        return;
    }
    if (m_btnUdpCmd.Update(touch)) {
        SendUdpCommand();
        return;
    }

    // Mando físico multi-jugador
    for (size_t p = 0; p < InputManager::MAX_PLAYERS; ++p) {
        const auto& pad = inputMgr.GetGamepad(p);
        if (!pad.isConnected) continue;

        if (pad.WasJustPressed(VirtualButton::B)) {
            AudioManager::Instance().PlayClick();
            SceneManager::Instance().GetModalStack().Pop();
            return;
        }
        if (pad.WasJustPressed(VirtualButton::L)) {
            OpenIpKeyboard();
            return;
        }
        if (pad.WasJustPressed(VirtualButton::R)) {
            SetLoopbackIp();
            return;
        }
        if (pad.WasJustPressed(VirtualButton::A)) {
            ToggleConnection();
            return;
        }
        if (pad.WasJustPressed(VirtualButton::X)) {
            SendTcpPing();
            return;
        }
        if (pad.WasJustPressed(VirtualButton::Y)) {
            SendUdpCommand();
            return;
        }
    }
}

void NetTestModal::Render() {
    Render2D::Begin();

    // 1. Fondo atenuado y marco principal del Modal
    Render2D::DrawBox(0, 0, 1280, 720, ColorRGBA{4, 8, 16, 195}, ColorRGBA{0, 0, 0, 0});
    Render2D::DrawBox(90, 52, 1100, 616, ColorRGBA{10, 18, 32, 245}, ColorRGBA::Cyan());

    // 2. Encabezado
    Render2D::DrawPixelString(NELEO_LOC("net.title"), 125, 74, 2, ColorRGBA::Yellow());
    Render2D::DrawPixelString(NELEO_LOC("net.hint"),
                              125, 102, 1, ColorRGBA{165, 205, 245, 255});

    // 3. Controles interactivos
    m_btnEditIp.Render();
    m_btnUseCemu.Render();
    m_btnConnect.Render();
    m_btnTcpPing.Render();
    m_btnUdpCmd.Render();
    m_btnClose.Render();

    // 4. Panel Izquierdo: Telemetría UDP + TCP (0 heap allocs)
    Render2D::DrawBox(125, 252, 645, 342, ColorRGBA{8, 14, 24, 235}, ColorRGBA{60, 130, 190, 255});
    Render2D::DrawPixelString(NELEO_LOC("net.telemetry.title"), 142, 266, 2, ColorRGBA::Cyan());

    const char* udpStateStr = NELEO_LOC_CSTR("net.state.disconnected");
    ColorRGBA udpStateCol = ColorRGBA{180, 180, 190, 255};
    switch (m_session.GetState()) {
        case NetSessionState::Connecting: udpStateStr = NELEO_LOC_CSTR("net.state.connecting"); udpStateCol = ColorRGBA::Yellow(); break;
        case NetSessionState::Connected:  udpStateStr = NELEO_LOC_CSTR("net.state.connected");  udpStateCol = ColorRGBA::Green();  break;
        case NetSessionState::TimedOut:   udpStateStr = NELEO_LOC_CSTR("net.state.timeout");    udpStateCol = ColorRGBA::Red();    break;
        default: break;
    }

    const auto& st = m_session.GetStats();
    char line[96];

    std::snprintf(line, sizeof(line), NELEO_LOC_CSTR("net.udp_state"), static_cast<unsigned>(m_udpPort), udpStateStr);
    Render2D::DrawPixelString(line, 142, 298, 2, udpStateCol);

    std::snprintf(line, sizeof(line), NELEO_LOC_CSTR("net.tcp_state"), static_cast<unsigned>(m_tcpPort),
                  m_session.IsTcpConnected() ? NELEO_LOC_CSTR("net.state.connected") : NELEO_LOC_CSTR("net.state.disconnected"));
    Render2D::DrawPixelString(line, 142, 326, 2, m_session.IsTcpConnected() ? ColorRGBA::Green() : ColorRGBA{180, 180, 190, 255});

    std::snprintf(line, sizeof(line), "RTT: %.1f ms | SRTT: %.1f ms | JITTER: %.1f ms",
                  st.rttMs, st.smoothedRttMs, st.jitterMs);
    Render2D::DrawPixelString(line, 142, 360, 2, ColorRGBA::White());

    std::snprintf(line, sizeof(line), "LOSS: %.1f%% | TX: %u | RX: %u | ACK: %u",
                  st.packetLossPct, st.packetsSent, st.packetsReceived, st.packetsAcked);
    Render2D::DrawPixelString(line, 142, 388, 2, ColorRGBA{200, 230, 255, 255});

    std::snprintf(line, sizeof(line), "BYTES TX: %llu | RX: %llu | SNAPS: %u",
                  static_cast<unsigned long long>(st.bytesSent),
                  static_cast<unsigned long long>(st.bytesReceived),
                  m_snapshotsRx);
    Render2D::DrawPixelString(line, 142, 416, 2, ColorRGBA{170, 210, 240, 255});

    Render2D::DrawPixelString(NELEO_LOC("net.last_tcp"), 142, 456, 2, ColorRGBA{180, 160, 255, 255});
    Render2D::DrawBox(142, 482, 610, 36, ColorRGBA{14, 18, 34, 240}, ColorRGBA{120, 100, 200, 255});
    Render2D::DrawPixelString(m_lastTcpMessage, 152, 492, 2, ColorRGBA::White());

    if (m_hasRemoteSnapshot) {
        std::snprintf(line, sizeof(line), NELEO_LOC_CSTR("net.entity_pos"),
                      m_lastRawSnapshot.position.x, m_lastRawSnapshot.position.y, m_lastRawSnapshot.position.z,
                      static_cast<unsigned>(m_lastRawSnapshot.tick));
        Render2D::DrawPixelString(line, 142, 536, 2, ColorRGBA{130, 255, 190, 255});
    } else {
        Render2D::DrawPixelString(NELEO_LOC("net.entity_wait"), 142, 536, 2, ColorRGBA{140, 160, 185, 255});
    }

    Render2D::DrawPixelString("SERVER PC: python tools/net_test_server.py", 142, 566, 1, ColorRGBA{145, 185, 220, 255});

    // 5. Panel Derecho: Mini-Radar 2D de Replicación + Interpolación (Vista X/Z)
    constexpr int radarX = 790;
    constexpr int radarY = 252;
    constexpr int radarW = 365;
    constexpr int radarH = 342;
    Render2D::DrawBox(radarX, radarY, radarW, radarH, ColorRGBA{8, 16, 24, 240}, ColorRGBA::Green());
    Render2D::DrawPixelString(NELEO_LOC("net.radar.title"), radarX + 16, radarY + 12, 1, ColorRGBA::Green());

    const int cx = radarX + radarW / 2;
    const int cy = radarY + radarH / 2 + 10;
    // Ejes de referencia
    Render2D::DrawBox(radarX + 20, cy, radarW - 40, 1, ColorRGBA{40, 75, 95, 255}, ColorRGBA{40, 75, 95, 255});
    Render2D::DrawBox(cx, radarY + 38, 1, radarH - 58, ColorRGBA{40, 75, 95, 255}, ColorRGBA{40, 75, 95, 255});

    if (m_hasRemoteSnapshot) {
        constexpr float scale = 28.0f; // 4.0m * 28 = 112 px de radio
        constexpr float maxOffX = static_cast<float>(radarW / 2 - 14);
        constexpr float maxOffY = static_cast<float>(radarH / 2 - 28);

        // Posición cruda del último paquete UDP (20 Hz) acotada al panel
        const int rawPx = cx + static_cast<int>(std::clamp(m_lastRawSnapshot.position.x * scale, -maxOffX, maxOffX));
        const int rawPy = cy + static_cast<int>(std::clamp(m_lastRawSnapshot.position.z * scale, -maxOffY, maxOffY));
        Render2D::DrawBox(rawPx - 5, rawPy - 5, 10, 10, ColorRGBA::Red(), ColorRGBA::White());

        // Posición suavizada por SnapshotInterpolator (100 ms de delay de interpolación)
        NetTransformSnapshot smooth{};
        const double renderTime = (m_localClockSec + m_serverTimeOffsetSec) - 0.10;
        if (m_interpolator.Sample(renderTime, smooth)) {
            const int smPx = cx + static_cast<int>(std::clamp(smooth.position.x * scale, -maxOffX, maxOffX));
            const int smPy = cy + static_cast<int>(std::clamp(smooth.position.z * scale, -maxOffY, maxOffY));
            Render2D::DrawBox(smPx - 6, smPy - 6, 12, 12, ColorRGBA::Cyan(), ColorRGBA::Yellow());
        }
    }

    // 6. Banner de feedback inferior
    if (m_feedbackTimer > 0.0f && m_feedbackText) {
        Render2D::DrawBox(125, 610, 1030, 40, ColorRGBA{12, 20, 34, 245}, m_feedbackColor);
        Render2D::DrawPixelString(m_feedbackText, 145, 622, 2, m_feedbackColor);
    }

    Render2D::End();
}

} // namespace Neleo

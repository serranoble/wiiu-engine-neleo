#pragma once

#include "Scene/ModalStack.hpp"
#include "Graphics/UIComponents.hpp"
#include "Net/NetSession.hpp"
#include "Net/NetReplication.hpp"
#include <cstdint>

namespace WiiU {

/**
 * @brief [Hito 145 - Cap. 20 Jason Gregory] Modal Interactivo de Prueba de Red (UDP + TCP)
 * y Teclado Nativo de Wii U (nn::swkbd).
 *
 * Se apila sobre SceneMenu vía ModalStack sin crear escenas adicionales ni asignaciones
 * dinámicas en el bucle de 60 FPS.
 */
class NetTestModal : public ISceneOverlay {
public:
    NetTestModal();
    ~NetTestModal() override;

    void OnAttach() override;
    void OnDetach() override;
    void Update(float dt) override;
    void Render() override;

    [[nodiscard]] bool BlocksSceneUpdate() const noexcept override { return true; }
    [[nodiscard]] bool BlocksSceneInput() const noexcept override { return true; }
    [[nodiscard]] const char* GetOverlayName() const noexcept override { return "NetTestModal"; }

private:
    static void OnUdpPacketStatic(const NetPacketHeader& header, const uint8_t* payload, size_t payloadSize, void* userData);
    static void OnTcpMessageStatic(const uint8_t* payload, size_t payloadSize, void* userData);

    void HandleUdpPacket(const NetPacketHeader& header, const uint8_t* payload, size_t payloadSize) noexcept;
    void HandleTcpMessage(const uint8_t* payload, size_t payloadSize) noexcept;

    void OpenIpKeyboard();
    void SetLoopbackIp();
    void ToggleConnection();
    void SendTcpPing();
    void SendUdpCommand();
    void RefreshButtonLabels();
    void SetFeedback(const char* msg, ColorRGBA color, float durationSec = 2.5f) noexcept;

    NetSession m_session;
    SnapshotInterpolator m_interpolator;
    NetTransformSnapshot m_lastRawSnapshot{};
    bool m_hasRemoteSnapshot{false};
    double m_localClockSec{0.0};
    double m_serverTimeOffsetSec{0.0};
    bool m_hasServerTimeSync{false};

    char m_serverIp[48]{"127.0.0.1"};
    uint16_t m_tcpPort{NetProtocol::DEFAULT_TCP_PORT};
    uint16_t m_udpPort{NetProtocol::DEFAULT_UDP_PORT};

    char m_lastTcpMessage[80]{"(sin mensajes TCP)"};
    uint32_t m_tcpPingCounter{0};
    uint32_t m_udpCmdCounter{0};
    uint32_t m_snapshotsRx{0};

    UIButton m_btnEditIp;
    UIButton m_btnUseCemu;
    UIButton m_btnConnect;
    UIButton m_btnTcpPing;
    UIButton m_btnUdpCmd;
    UIButton m_btnClose;

    const char* m_feedbackText{nullptr};
    ColorRGBA m_feedbackColor{ColorRGBA::Green()};
    float m_feedbackTimer{0.0f};
};

} // namespace WiiU

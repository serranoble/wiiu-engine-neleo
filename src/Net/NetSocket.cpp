#include "Net/NetSocket.hpp"
#include "Net/NetBitStream.hpp"
#include "Net/NetReplication.hpp"
#include <atomic>
#include <cmath>
#include <cstring>

#if defined(_WIN32) && !defined(__WIIU__) && !defined(__wiiu__)
#define WIN32_LEAN_AND_MEAN
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

namespace WiiU {

namespace {

std::atomic<bool> s_netInitialized{false};

#if defined(_WIN32) && !defined(__WIIU__) && !defined(__wiiu__)
using NativeSocket = SOCKET;
constexpr NativeSocket NATIVE_INVALID = INVALID_SOCKET;

inline int GetLastNetError() noexcept { return WSAGetLastError(); }
inline bool IsWouldBlockError(int err) noexcept {
    return err == WSAEWOULDBLOCK || err == WSAEINPROGRESS;
}
inline void CloseNativeSocket(NativeSocket s) noexcept {
    if (s != INVALID_SOCKET) {
        closesocket(s);
    }
}
inline bool SetNonBlocking(NativeSocket s) noexcept {
    u_long mode = 1;
    return ioctlsocket(s, FIONBIO, &mode) == 0;
}
#else
using NativeSocket = int;
constexpr NativeSocket NATIVE_INVALID = -1;

inline int GetLastNetError() noexcept { return errno; }
inline bool IsWouldBlockError(int err) noexcept {
    return err == EWOULDBLOCK || err == EAGAIN || err == EINPROGRESS;
}
inline void CloseNativeSocket(NativeSocket s) noexcept {
    if (s >= 0) {
        close(s);
    }
}
inline bool SetNonBlocking(NativeSocket s) noexcept {
    const int flags = fcntl(s, F_GETFL, 0);
    if (flags < 0) {
        return false;
    }
    return fcntl(s, F_SETFL, flags | O_NONBLOCK) == 0;
}
#endif

inline sockaddr_in ToSockAddrIn(const NetAddress& addr) noexcept {
    sockaddr_in sa{};
    sa.sin_family = AF_INET;
    sa.sin_port = htons(addr.port);
    sa.sin_addr.s_addr = htonl(addr.ToHostU32());
    return sa;
}

inline NetAddress FromSockAddrIn(const sockaddr_in& sa) noexcept {
    const uint32_t ipHost = ntohl(sa.sin_addr.s_addr);
    const uint16_t portHost = ntohs(sa.sin_port);
    return NetAddress::FromIPv4(
        static_cast<uint8_t>((ipHost >> 24) & 0xFFu),
        static_cast<uint8_t>((ipHost >> 16) & 0xFFu),
        static_cast<uint8_t>((ipHost >> 8) & 0xFFu),
        static_cast<uint8_t>(ipHost & 0xFFu),
        portHost
    );
}

} // namespace

// =============================================================================
// NetPlatform
// =============================================================================

bool NetPlatform::Initialize() noexcept {
    if (s_netInitialized.load(std::memory_order_acquire)) {
        return true;
    }

#if defined(_WIN32) && !defined(__WIIU__) && !defined(__wiiu__)
    WSADATA wsaData{};
    if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) {
        return false;
    }
#endif
    // En Nintendo Wii U (WUT), la biblioteca nsysnet/POSIX se inicializa automáticamente
    // por el runtime wut-crt antes de main().
    s_netInitialized.store(true, std::memory_order_release);
    return true;
}

void NetPlatform::Shutdown() noexcept {
    if (!s_netInitialized.exchange(false, std::memory_order_acq_rel)) {
        return;
    }
#if defined(_WIN32) && !defined(__WIIU__) && !defined(__wiiu__)
    WSACleanup();
#endif
}

bool NetPlatform::IsInitialized() noexcept {
    return s_netInitialized.load(std::memory_order_acquire);
}

bool NetPlatform::VerifyNetIntegrity() noexcept {
    // 1. Verificar parseo IPv4 sin heap
    NetAddress parsed{};
    if (!NetAddress::ParseIPv4("192.168.1.42", 7778, parsed)) return false;
    if (parsed != NetAddress::FromIPv4(192, 168, 1, 42, 7778)) return false;
    NetAddress bad{};
    if (NetAddress::ParseIPv4("999.1.1.1", 7778, bad)) return false;

    // 2. Verificar aritmética modular de secuencias (wrap-around 65535 -> 0)
    if (!SequenceGreaterThan(1u, 0u)) return false;
    if (!SequenceGreaterThan(0u, 65535u)) return false;
    if (SequenceGreaterThan(65535u, 0u)) return false;

    // 3. Verificar serialización Big-Endian Network Byte Order + round-trip
    NetPacketBuffer buf{};
    NetPacketHeader hdr{};
    hdr.type = NetMessageType::WorldSnapshot;
    hdr.sequence = 0x1234u;
    hdr.ack = 0x5678u;
    hdr.ackBitfield = 0xA5A5F00Du;
    hdr.tick = 42u;

    NetTransformSnapshot txSnap{};
    txSnap.netId = 7u;
    txSnap.tick = 42u;
    txSnap.timestampSec = 1.25;
    txSnap.position = Point3D{12.5f, -3.25f, 8.0f};
    txSnap.rotation = Quaternion{0.0f, 0.70710678f, 0.0f, 0.70710678f};
    txSnap.velocity = Point3D{1.0f, 0.0f, -2.0f};

    NetBitWriter writer(buf.data(), buf.size());
    if (!writer.WriteHeader(hdr)) return false;
    if (writer.GetBytesWritten() != NetProtocol::HEADER_BYTES) return false;
    // Verificar orden Big-Endian exacto del magic "WUN1" = 0x57 0x55 0x4E 0x31
    if (buf[0] != 0x57u || buf[1] != 0x55u || buf[2] != 0x4Eu || buf[3] != 0x31u) return false;
    if (!SerializeSnapshot(writer, txSnap)) return false;
    if (writer.HasOverflowed()) return false;

    NetBitReader reader(buf.data(), writer.GetBytesWritten());
    NetPacketHeader rxHdr{};
    NetTransformSnapshot rxSnap{};
    if (!reader.ReadHeader(rxHdr)) return false;
    if (rxHdr.sequence != hdr.sequence || rxHdr.ackBitfield != hdr.ackBitfield) return false;
    if (!DeserializeSnapshot(reader, rxSnap)) return false;
    if (rxSnap.netId != txSnap.netId || std::abs(rxSnap.position.x - 12.5f) > 0.001f) return false;

    // 4. Verificar SnapshotInterpolator (LERP + SLERP a mitad de intervalo)
    SnapshotInterpolator interp;
    NetTransformSnapshot s0{};
    s0.netId = 1u; s0.tick = 1u; s0.timestampSec = 1.0; s0.position = Point3D{0.0f, 0.0f, 0.0f};
    NetTransformSnapshot s1{};
    s1.netId = 1u; s1.tick = 2u; s1.timestampSec = 2.0; s1.position = Point3D{10.0f, 4.0f, -6.0f};
    interp.PushSnapshot(s0);
    interp.PushSnapshot(s1);

    NetTransformSnapshot mid{};
    if (!interp.Sample(1.5, mid)) return false;
    if (std::abs(mid.position.x - 5.0f) > 0.01f || std::abs(mid.position.y - 2.0f) > 0.01f) return false;

    return true;
}

// =============================================================================
// UdpSocket
// =============================================================================

UdpSocket::~UdpSocket() noexcept {
    Close();
}

UdpSocket::UdpSocket(UdpSocket&& other) noexcept
    : m_handle(other.m_handle), m_boundPort(other.m_boundPort) {
    other.m_handle = INVALID_HANDLE;
    other.m_boundPort = 0;
}

UdpSocket& UdpSocket::operator=(UdpSocket&& other) noexcept {
    if (this != &other) {
        Close();
        m_handle = other.m_handle;
        m_boundPort = other.m_boundPort;
        other.m_handle = INVALID_HANDLE;
        other.m_boundPort = 0;
    }
    return *this;
}

NetResult UdpSocket::Open(uint16_t bindPort) noexcept {
    if (!NetPlatform::IsInitialized()) {
        return NetResult::NotInitialized;
    }
    Close();

    NativeSocket sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (sock == NATIVE_INVALID) {
        return NetResult::SocketError;
    }

    if (!SetNonBlocking(sock)) {
        CloseNativeSocket(sock);
        return NetResult::SocketError;
    }

    sockaddr_in local{};
    local.sin_family = AF_INET;
    local.sin_addr.s_addr = htonl(INADDR_ANY);
    local.sin_port = htons(bindPort);

    if (bind(sock, reinterpret_cast<const sockaddr*>(&local), sizeof(local)) != 0) {
        CloseNativeSocket(sock);
        return NetResult::SocketError;
    }

    sockaddr_in actual{};
#if defined(_WIN32) && !defined(__WIIU__) && !defined(__wiiu__)
    int actualLen = sizeof(actual);
#else
    socklen_t actualLen = sizeof(actual);
#endif
    if (getsockname(sock, reinterpret_cast<sockaddr*>(&actual), &actualLen) == 0) {
        m_boundPort = ntohs(actual.sin_port);
    } else {
        m_boundPort = bindPort;
    }

    m_handle = static_cast<intptr_t>(sock);
    return NetResult::Ok;
}

void UdpSocket::Close() noexcept {
    if (m_handle != INVALID_HANDLE) {
        CloseNativeSocket(static_cast<NativeSocket>(m_handle));
        m_handle = INVALID_HANDLE;
        m_boundPort = 0;
    }
}

NetResult UdpSocket::SendTo(const NetAddress& dest, const uint8_t* data, size_t size, size_t& outSent) noexcept {
    outSent = 0;
    if (!IsOpen()) return NetResult::NotInitialized;
    if (!dest.IsValid() || !data || size == 0u) return NetResult::InvalidAddress;

    const sockaddr_in sa = ToSockAddrIn(dest);
    const int sent = static_cast<int>(sendto(
        static_cast<NativeSocket>(m_handle),
        reinterpret_cast<const char*>(data),
        static_cast<int>(size),
        0,
        reinterpret_cast<const sockaddr*>(&sa),
        sizeof(sa)
    ));

    if (sent < 0) {
        return IsWouldBlockError(GetLastNetError()) ? NetResult::WouldBlock : NetResult::SocketError;
    }
    outSent = static_cast<size_t>(sent);
    return NetResult::Ok;
}

NetResult UdpSocket::ReceiveFrom(NetAddress& outFrom, uint8_t* outBuf, size_t bufCap, size_t& outReceived) noexcept {
    outReceived = 0;
    if (!IsOpen()) return NetResult::NotInitialized;
    if (!outBuf || bufCap == 0u) return NetResult::BufferTooSmall;

    sockaddr_in from{};
#if defined(_WIN32) && !defined(__WIIU__) && !defined(__wiiu__)
    int fromLen = sizeof(from);
#else
    socklen_t fromLen = sizeof(from);
#endif

    const int recvd = static_cast<int>(recvfrom(
        static_cast<NativeSocket>(m_handle),
        reinterpret_cast<char*>(outBuf),
        static_cast<int>(bufCap),
        0,
        reinterpret_cast<sockaddr*>(&from),
        &fromLen
    ));

    if (recvd < 0) {
        return IsWouldBlockError(GetLastNetError()) ? NetResult::WouldBlock : NetResult::SocketError;
    }
    if (recvd == 0) {
        return NetResult::WouldBlock;
    }

    outFrom = FromSockAddrIn(from);
    outReceived = static_cast<size_t>(recvd);
    return NetResult::Ok;
}

// =============================================================================
// TcpSocket
// =============================================================================

TcpSocket::~TcpSocket() noexcept {
    Close();
}

TcpSocket::TcpSocket(TcpSocket&& other) noexcept
    : m_handle(other.m_handle), m_connecting(other.m_connecting), m_connected(other.m_connected) {
    other.m_handle = INVALID_HANDLE;
    other.m_connecting = false;
    other.m_connected = false;
}

TcpSocket& TcpSocket::operator=(TcpSocket&& other) noexcept {
    if (this != &other) {
        Close();
        m_handle = other.m_handle;
        m_connecting = other.m_connecting;
        m_connected = other.m_connected;
        other.m_handle = INVALID_HANDLE;
        other.m_connecting = false;
        other.m_connected = false;
    }
    return *this;
}

NetResult TcpSocket::ConnectNonBlocking(const NetAddress& remote) noexcept {
    if (!NetPlatform::IsInitialized()) return NetResult::NotInitialized;
    if (!remote.IsValid()) return NetResult::InvalidAddress;
    Close();

    NativeSocket sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (sock == NATIVE_INVALID) {
        return NetResult::SocketError;
    }

    if (!SetNonBlocking(sock)) {
        CloseNativeSocket(sock);
        return NetResult::SocketError;
    }

    const sockaddr_in sa = ToSockAddrIn(remote);
    const int rc = connect(sock, reinterpret_cast<const sockaddr*>(&sa), sizeof(sa));
    if (rc == 0) {
        m_handle = static_cast<intptr_t>(sock);
        m_connecting = false;
        m_connected = true;
        return NetResult::Ok;
    }

    if (IsWouldBlockError(GetLastNetError())) {
        m_handle = static_cast<intptr_t>(sock);
        m_connecting = true;
        m_connected = false;
        return NetResult::WouldBlock;
    }

    CloseNativeSocket(sock);
    return NetResult::SocketError;
}

NetResult TcpSocket::PollConnect(bool& outConnected) noexcept {
    outConnected = m_connected;
    if (!IsOpen()) return NetResult::NotInitialized;
    if (m_connected) return NetResult::Ok;
    if (!m_connecting) return NetResult::Disconnected;

    NativeSocket sock = static_cast<NativeSocket>(m_handle);
#if !defined(_WIN32) || defined(__WIIU__) || defined(__wiiu__)
    if (sock < 0 || sock >= FD_SETSIZE) {
        Close();
        return NetResult::SocketError;
    }
#endif
    fd_set writeSet;
    fd_set errSet;
    FD_ZERO(&writeSet);
    FD_ZERO(&errSet);
    FD_SET(sock, &writeSet);
    FD_SET(sock, &errSet);

    timeval tv{0, 0}; // 0 us -> estrictamente no bloqueante
    const int ready = select(static_cast<int>(sock) + 1, nullptr, &writeSet, &errSet, &tv);
    if (ready < 0) {
        Close();
        return NetResult::SocketError;
    }
    if (ready == 0) {
        return NetResult::WouldBlock;
    }

    int soErr = 0;
#if defined(_WIN32) && !defined(__WIIU__) && !defined(__wiiu__)
    int optLen = sizeof(soErr);
#else
    socklen_t optLen = sizeof(soErr);
#endif
    if (getsockopt(sock, SOL_SOCKET, SO_ERROR, reinterpret_cast<char*>(&soErr), &optLen) != 0 || soErr != 0) {
        Close();
        return NetResult::Disconnected;
    }

    if (FD_ISSET(sock, &writeSet)) {
        m_connecting = false;
        m_connected = true;
        outConnected = true;
        return NetResult::Ok;
    }

    return NetResult::WouldBlock;
}

void TcpSocket::Close() noexcept {
    if (m_handle != INVALID_HANDLE) {
        CloseNativeSocket(static_cast<NativeSocket>(m_handle));
        m_handle = INVALID_HANDLE;
    }
    m_connecting = false;
    m_connected = false;
}

NetResult TcpSocket::Send(const uint8_t* data, size_t size, size_t& outSent) noexcept {
    outSent = 0;
    if (!IsOpen() || !m_connected) return NetResult::Disconnected;
    if (!data || size == 0u) return NetResult::Ok;

    const int sent = static_cast<int>(send(
        static_cast<NativeSocket>(m_handle),
        reinterpret_cast<const char*>(data),
        static_cast<int>(size),
        0
    ));

    if (sent < 0) {
        if (IsWouldBlockError(GetLastNetError())) {
            return NetResult::WouldBlock;
        }
        Close();
        return NetResult::SocketError;
    }
    outSent = static_cast<size_t>(sent);
    return NetResult::Ok;
}

NetResult TcpSocket::Receive(uint8_t* outBuf, size_t bufCap, size_t& outReceived) noexcept {
    outReceived = 0;
    if (!IsOpen() || !m_connected) return NetResult::Disconnected;
    if (!outBuf || bufCap == 0u) return NetResult::BufferTooSmall;

    const int recvd = static_cast<int>(recv(
        static_cast<NativeSocket>(m_handle),
        reinterpret_cast<char*>(outBuf),
        static_cast<int>(bufCap),
        0
    ));

    if (recvd == 0) {
        Close();
        return NetResult::Disconnected;
    }
    if (recvd < 0) {
        return IsWouldBlockError(GetLastNetError()) ? NetResult::WouldBlock : NetResult::SocketError;
    }

    outReceived = static_cast<size_t>(recvd);
    return NetResult::Ok;
}

} // namespace WiiU

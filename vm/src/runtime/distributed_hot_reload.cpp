#include "vm/runtime/distributed_hot_reload.h"
#include "vm/runtime/vm.h"
#include "vm/runtime/hot_reload.h"
#include "vm/bytecode/binary_format.h"
#include <cstring>
#include <algorithm>

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#pragma comment(lib, "ws2_32.lib")
using socket_t = SOCKET;
#define INVALID_SOCK INVALID_SOCKET
#define CLOSE_SOCKET closesocket
#else
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <fcntl.h>
using socket_t = int;
#define INVALID_SOCK (-1)
#define CLOSE_SOCKET ::close
#endif

namespace lpc {
namespace vm {

void ReloadProtocol::WriteU16(std::vector<std::uint8_t> &buf, std::uint16_t v) {
    buf.push_back(static_cast<std::uint8_t>(v & 0xFF));
    buf.push_back(static_cast<std::uint8_t>((v >> 8) & 0xFF));
}

void ReloadProtocol::WriteU32(std::vector<std::uint8_t> &buf, std::uint32_t v) {
    buf.push_back(static_cast<std::uint8_t>(v & 0xFF));
    buf.push_back(static_cast<std::uint8_t>((v >> 8) & 0xFF));
    buf.push_back(static_cast<std::uint8_t>((v >> 16) & 0xFF));
    buf.push_back(static_cast<std::uint8_t>((v >> 24) & 0xFF));
}

void ReloadProtocol::WriteU64(std::vector<std::uint8_t> &buf, std::uint64_t v) {
    for (int i = 0; i < 8; ++i)
        buf.push_back(static_cast<std::uint8_t>((v >> (i * 8)) & 0xFF));
}

void ReloadProtocol::WriteStr(std::vector<std::uint8_t> &buf, const std::string &s) {
    WriteU32(buf, static_cast<std::uint32_t>(s.size()));
    buf.insert(buf.end(), s.begin(), s.end());
}

bool ReloadProtocol::ReadU16(const std::uint8_t *data, std::size_t len, std::size_t &pos, std::uint16_t &out) {
    if (pos + 2 > len) return false;
    out = static_cast<std::uint16_t>(data[pos]) | (static_cast<std::uint16_t>(data[pos + 1]) << 8);
    pos += 2;
    return true;
}

bool ReloadProtocol::ReadU32(const std::uint8_t *data, std::size_t len, std::size_t &pos, std::uint32_t &out) {
    if (pos + 4 > len) return false;
    out = static_cast<std::uint32_t>(data[pos]);
    out |= static_cast<std::uint32_t>(data[pos + 1]) << 8;
    out |= static_cast<std::uint32_t>(data[pos + 2]) << 16;
    out |= static_cast<std::uint32_t>(data[pos + 3]) << 24;
    pos += 4;
    return true;
}

bool ReloadProtocol::ReadU64(const std::uint8_t *data, std::size_t len, std::size_t &pos, std::uint64_t &out) {
    if (pos + 8 > len) return false;
    out = 0;
    for (int i = 0; i < 8; ++i)
        out |= static_cast<std::uint64_t>(data[pos + i]) << (i * 8);
    pos += 8;
    return true;
}

bool ReloadProtocol::ReadStr(const std::uint8_t *data, std::size_t len, std::size_t &pos, std::string &out) {
    std::uint32_t slen = 0;
    if (!ReadU32(data, len, pos, slen)) return false;
    if (pos + slen > len) return false;
    out.assign(reinterpret_cast<const char *>(data + pos), slen);
    pos += slen;
    return true;
}

std::vector<std::uint8_t> ReloadProtocol::Encode(const ReloadMessage &msg) {
    std::vector<std::uint8_t> buf;
    buf.push_back(0x4C);
    buf.push_back(0x50);
    buf.push_back(0x43);
    buf.push_back(0x52);
    buf.push_back(static_cast<std::uint8_t>(msg.type));
    WriteU32(buf, msg.request_id);
    WriteStr(buf, msg.module_name);
    WriteU64(buf, msg.version_id);
    WriteStr(buf, msg.chunk_data);
    WriteStr(buf, msg.compat_level);
    WriteStr(buf, msg.migration_data);
    WriteStr(buf, msg.status_json);
    WriteStr(buf, msg.error_message);
    buf.push_back(msg.success ? 1 : 0);

    std::vector<std::uint8_t> frame;
    WriteU32(frame, static_cast<std::uint32_t>(buf.size()));
    frame.insert(frame.end(), buf.begin(), buf.end());
    return frame;
}

bool ReloadProtocol::Decode(const std::uint8_t *data, std::size_t len, ReloadMessage &out_msg) {
    std::size_t pos = 0;
    if (len < 4) return false;
    if (data[0] != 0x4C || data[1] != 0x50 || data[2] != 0x43 || data[3] != 0x52) return false;
    pos = 4;

    std::uint8_t type_byte = 0;
    if (pos >= len) return false;
    type_byte = data[pos++];
    out_msg.type = static_cast<ReloadMsgType>(type_byte);

    if (!ReadU32(data, len, pos, out_msg.request_id)) return false;
    if (!ReadStr(data, len, pos, out_msg.module_name)) return false;
    if (!ReadU64(data, len, pos, out_msg.version_id)) return false;
    if (!ReadStr(data, len, pos, out_msg.chunk_data)) return false;
    if (!ReadStr(data, len, pos, out_msg.compat_level)) return false;
    if (!ReadStr(data, len, pos, out_msg.migration_data)) return false;
    if (!ReadStr(data, len, pos, out_msg.status_json)) return false;
    if (!ReadStr(data, len, pos, out_msg.error_message)) return false;
    if (pos >= len) return false;
    out_msg.success = (data[pos++] != 0);

    return true;
}

HotReloadCoordinator::HotReloadCoordinator(Vm &vm, SendFn send_fn, BroadcastFn broadcast_fn)
    : vm_(vm), send_fn_(std::move(send_fn)), broadcast_fn_(std::move(broadcast_fn)) {}

void HotReloadCoordinator::AddPeer(std::uint64_t peer_id, const std::string &host, int port) {
    PeerInfo info;
    info.host = host;
    info.port = port;
    info.peer_id = peer_id;
    info.connected = true;
    peers_[peer_id] = info;

    ReloadMessage hello;
    hello.type = ReloadMsgType::Hello;
    hello.request_id = next_request_id();
    hello.module_name = "";
    auto data = ReloadProtocol::Encode(hello);
    send_fn_(peer_id, data);
}

void HotReloadCoordinator::RemovePeer(std::uint64_t peer_id) {
    peers_.erase(peer_id);
}

void HotReloadCoordinator::OnMessage(std::uint64_t from_peer_id, const ReloadMessage &msg) {
    switch (msg.type) {
        case ReloadMsgType::Hello: {
            ReloadMessage ack;
            ack.type = ReloadMsgType::HelloAck;
            ack.request_id = msg.request_id;
            ack.success = true;
            send_fn_(from_peer_id, ReloadProtocol::Encode(ack));
            break;
        }
        case ReloadMsgType::HelloAck: {
            auto it = peers_.find(from_peer_id);
            if (it != peers_.end()) it->second.connected = true;
            break;
        }
        case ReloadMsgType::PrepareReload: {
            Chunk candidate;
            if (!DeserializeChunk(msg.chunk_data, &candidate)) {
                ReloadMessage resp;
                resp.type = ReloadMsgType::PrepareResult;
                resp.request_id = msg.request_id;
                resp.success = false;
                resp.error_message = "failed to deserialize chunk";
                send_fn_(from_peer_id, ReloadProtocol::Encode(resp));
                return;
            }

            HotReloadLevel level = HotReloadLevel::L0;
            if (msg.compat_level == "L1") level = HotReloadLevel::L1;
            else if (msg.compat_level == "L2") level = HotReloadLevel::L2;

            std::uint64_t cand_ver = 0;
            HotReloadCompatReport report;
            RuntimeError e = vm_.PrepareHotReload(msg.module_name, candidate, level, &cand_ver, &report);

            ReloadMessage resp;
            resp.type = ReloadMsgType::PrepareResult;
            resp.request_id = msg.request_id;
            resp.module_name = msg.module_name;
            resp.version_id = cand_ver;
            resp.success = e.ok();
            resp.error_message = e.ok() ? "" : e.message;
            send_fn_(from_peer_id, ReloadProtocol::Encode(resp));
            break;
        }
        case ReloadMsgType::ActivateReload: {
            std::uint64_t prev = 0;
            RuntimeError e = vm_.ActivateHotReload(msg.module_name, msg.version_id, &prev);

            ReloadMessage resp;
            resp.type = ReloadMsgType::ActivateResult;
            resp.request_id = msg.request_id;
            resp.module_name = msg.module_name;
            resp.version_id = msg.version_id;
            resp.success = e.ok();
            resp.error_message = e.ok() ? "" : e.message;
            send_fn_(from_peer_id, ReloadProtocol::Encode(resp));
            break;
        }
        case ReloadMsgType::RollbackReload: {
            RuntimeError e = vm_.RollbackHotReload(msg.module_name, msg.version_id);

            ReloadMessage resp;
            resp.type = ReloadMsgType::RollbackResult;
            resp.request_id = msg.request_id;
            resp.module_name = msg.module_name;
            resp.version_id = msg.version_id;
            resp.success = e.ok();
            resp.error_message = e.ok() ? "" : e.message;
            send_fn_(from_peer_id, ReloadProtocol::Encode(resp));
            break;
        }
        case ReloadMsgType::StatusQuery: {
            auto status = vm_.GetHotReloadStatus(msg.module_name);

            ReloadMessage resp;
            resp.type = ReloadMsgType::StatusResponse;
            resp.request_id = msg.request_id;
            resp.module_name = msg.module_name;
            resp.version_id = status.active_version;
            resp.success = true;
            resp.status_json = "{\"active_version\":" + std::to_string(status.active_version) +
                               ",\"prepared_version\":" + std::to_string(status.prepared_version) + "}";
            send_fn_(from_peer_id, ReloadProtocol::Encode(resp));
            break;
        }
        case ReloadMsgType::SyncState: {
            ReloadMessage resp;
            resp.type = ReloadMsgType::SyncAck;
            resp.request_id = msg.request_id;
            resp.success = true;
            send_fn_(from_peer_id, ReloadProtocol::Encode(resp));
            break;
        }
        case ReloadMsgType::Heartbeat: {
            break;
        }
        default:
            break;
    }
}

RuntimeError HotReloadCoordinator::PrepareAndBroadcast(
    const std::string &module_name,
    const std::string &chunk_data,
    const std::string &compat_level,
    const std::string &migration_data) {

    Chunk candidate;
    if (!DeserializeChunk(chunk_data, &candidate)) {
        return RuntimeError::Error(RuntimeErrorCode::InvalidOperand, "failed to deserialize chunk for broadcast");
    }

    HotReloadLevel level = HotReloadLevel::L0;
    if (compat_level == "L1") level = HotReloadLevel::L1;
    else if (compat_level == "L2") level = HotReloadLevel::L2;

    std::uint64_t cand_ver = 0;
    HotReloadCompatReport report;
    RuntimeError e = vm_.PrepareHotReload(module_name, candidate, level, &cand_ver, &report);
    if (!e.ok()) return e;

    ReloadMessage msg;
    msg.type = ReloadMsgType::PrepareReload;
    msg.request_id = next_request_id();
    msg.module_name = module_name;
    msg.version_id = cand_ver;
    msg.chunk_data = chunk_data;
    msg.compat_level = compat_level;
    msg.migration_data = migration_data;
    msg.success = true;

    broadcast_fn_(ReloadProtocol::Encode(msg));
    return RuntimeError::Ok();
}

RuntimeError HotReloadCoordinator::ActivateAndBroadcast(const std::string &module_name, std::uint64_t version_id) {
    std::uint64_t prev = 0;
    RuntimeError e = vm_.ActivateHotReload(module_name, version_id, &prev);
    if (!e.ok()) return e;

    ReloadMessage msg;
    msg.type = ReloadMsgType::ActivateReload;
    msg.request_id = next_request_id();
    msg.module_name = module_name;
    msg.version_id = version_id;
    msg.success = true;

    broadcast_fn_(ReloadProtocol::Encode(msg));
    return RuntimeError::Ok();
}

RuntimeError HotReloadCoordinator::RollbackAndBroadcast(const std::string &module_name, std::uint64_t version_id) {
    RuntimeError e = vm_.RollbackHotReload(module_name, version_id);
    if (!e.ok()) return e;

    ReloadMessage msg;
    msg.type = ReloadMsgType::RollbackReload;
    msg.request_id = next_request_id();
    msg.module_name = module_name;
    msg.version_id = version_id;
    msg.success = true;

    broadcast_fn_(ReloadProtocol::Encode(msg));
    return RuntimeError::Ok();
}

void HotReloadCoordinator::BroadcastStatus() {
    ReloadMessage msg;
    msg.type = ReloadMsgType::StatusQuery;
    msg.request_id = next_request_id();
    broadcast_fn_(ReloadProtocol::Encode(msg));
}

void HotReloadCoordinator::SyncStateToPeer(std::uint64_t peer_id) {
    ReloadMessage msg;
    msg.type = ReloadMsgType::SyncState;
    msg.request_id = next_request_id();
    send_fn_(peer_id, ReloadProtocol::Encode(msg));
}

HotReloadTcpTransport::HotReloadTcpTransport() {
#ifdef _WIN32
    static bool wsa_init = false;
    if (!wsa_init) {
        WSADATA wsa;
        WSAStartup(MAKEWORD(2, 2), &wsa);
        wsa_init = true;
    }
#endif
}

HotReloadTcpTransport::~HotReloadTcpTransport() {
    Close();
}

bool HotReloadTcpTransport::Listen(int port) {
    listen_fd_ = static_cast<int>(::socket(AF_INET, SOCK_STREAM, 0));
    if (listen_fd_ == INVALID_SOCK) return false;

    int opt = 1;
    setsockopt(static_cast<socket_t>(listen_fd_), SOL_SOCKET, SO_REUSEADDR,
               reinterpret_cast<const char *>(&opt), sizeof(opt));

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(static_cast<std::uint16_t>(port));

    if (bind(static_cast<socket_t>(listen_fd_), reinterpret_cast<sockaddr *>(&addr), sizeof(addr)) != 0) {
        CLOSE_SOCKET(static_cast<socket_t>(listen_fd_));
        listen_fd_ = -1;
        return false;
    }

    if (::listen(static_cast<socket_t>(listen_fd_), 5) != 0) {
        CLOSE_SOCKET(static_cast<socket_t>(listen_fd_));
        listen_fd_ = -1;
        return false;
    }

    listen_port_ = port;
    return true;
}

bool HotReloadTcpTransport::Connect(const std::string &host, int port, std::uint64_t *out_peer_id) {
    int fd = static_cast<int>(::socket(AF_INET, SOCK_STREAM, 0));
    if (fd == INVALID_SOCK) return false;

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(static_cast<std::uint16_t>(port));
    inet_pton(AF_INET, host.c_str(), &addr.sin_addr);

    if (::connect(static_cast<socket_t>(fd), reinterpret_cast<sockaddr *>(&addr), sizeof(addr)) != 0) {
        CLOSE_SOCKET(static_cast<socket_t>(fd));
        return false;
    }

    std::uint64_t conn_id = next_conn_id_++;
    PeerConn conn;
    conn.fd = fd;
    conn.host = host;
    conn.port = port;
    conn.peer_id = conn_id;
    connections_[conn_id] = std::move(conn);

    if (out_peer_id) *out_peer_id = conn_id;
    return true;
}

void HotReloadTcpTransport::Close() {
    if (listen_fd_ >= 0) {
        CLOSE_SOCKET(static_cast<socket_t>(listen_fd_));
        listen_fd_ = -1;
    }
    for (auto &[id, conn] : connections_) {
        if (conn.fd >= 0) CLOSE_SOCKET(static_cast<socket_t>(conn.fd));
    }
    connections_.clear();
}

void HotReloadTcpTransport::Poll(int timeout_ms) {
    if (listen_fd_ >= 0) {
        fd_set read_fds;
        FD_ZERO(&read_fds);
        FD_SET(static_cast<socket_t>(listen_fd_), &read_fds);

        struct timeval tv;
        tv.tv_sec = 0;
        tv.tv_usec = timeout_ms * 1000;

        if (::select(static_cast<int>(listen_fd_) + 1, &read_fds, nullptr, nullptr, &tv) > 0) {
            sockaddr_in client_addr{};
            socklen_t client_len = sizeof(client_addr);
            int client_fd = static_cast<int>(::accept(static_cast<socket_t>(listen_fd_),
                reinterpret_cast<sockaddr *>(&client_addr), &client_len));
            if (client_fd != INVALID_SOCK) {
                std::uint64_t conn_id = next_conn_id_++;
                PeerConn conn;
                conn.fd = client_fd;
                char ip[INET_ADDRSTRLEN];
                inet_ntop(AF_INET, &client_addr.sin_addr, ip, sizeof(ip));
                conn.host = ip;
                conn.port = ntohs(client_addr.sin_port);
                conn.peer_id = conn_id;
                connections_[conn_id] = std::move(conn);
            }
        }
    }

    for (auto &[id, conn] : connections_) {
        if (conn.fd < 0) continue;

        fd_set read_fds;
        FD_ZERO(&read_fds);
        FD_SET(static_cast<socket_t>(conn.fd), &read_fds);

        struct timeval tv;
        tv.tv_sec = 0;
        tv.tv_usec = 1000;

        if (::select(static_cast<int>(conn.fd) + 1, &read_fds, nullptr, nullptr, &tv) > 0) {
            char buf[4096];
            int n = ::recv(static_cast<socket_t>(conn.fd), buf, sizeof(buf), 0);
            if (n <= 0) {
                CLOSE_SOCKET(static_cast<socket_t>(conn.fd));
                conn.fd = -1;
                continue;
            }
            conn.recv_buf.insert(conn.recv_buf.end(), buf, buf + n);

            while (conn.recv_buf.size() >= 4) {
                std::uint32_t frame_len = static_cast<std::uint32_t>(conn.recv_buf[0]);
                frame_len |= static_cast<std::uint32_t>(conn.recv_buf[1]) << 8;
                frame_len |= static_cast<std::uint32_t>(conn.recv_buf[2]) << 16;
                frame_len |= static_cast<std::uint32_t>(conn.recv_buf[3]) << 24;

                if (conn.recv_buf.size() < 4 + frame_len) break;

                ReloadMessage msg;
                if (ReloadProtocol::Decode(conn.recv_buf.data() + 4, frame_len, msg)) {
                    if (on_message_) on_message_(id, msg);
                }
                conn.recv_buf.erase(conn.recv_buf.begin(), conn.recv_buf.begin() + 4 + frame_len);
            }
        }
    }
}

void HotReloadTcpTransport::Send(std::uint64_t peer_id, const std::vector<std::uint8_t> &data) {
    auto it = connections_.find(peer_id);
    if (it == connections_.end() || it->second.fd < 0) return;
    ::send(static_cast<socket_t>(it->second.fd),
           reinterpret_cast<const char *>(data.data()),
           static_cast<int>(data.size()), 0);
}

void HotReloadTcpTransport::Broadcast(const std::vector<std::uint8_t> &data) {
    for (auto &[id, conn] : connections_) {
        if (conn.fd < 0) continue;
        ::send(static_cast<socket_t>(conn.fd),
               reinterpret_cast<const char *>(data.data()),
               static_cast<int>(data.size()), 0);
    }
}

} // namespace vm
} // namespace lpc

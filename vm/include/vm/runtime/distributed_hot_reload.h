#ifndef LPC_VM_DISTRIBUTED_HOT_RELOAD_H
#define LPC_VM_DISTRIBUTED_HOT_RELOAD_H

#include <string>
#include <vector>
#include <unordered_map>
#include <functional>
#include <cstdint>
#include <memory>
#include "vm/runtime/error.h"

namespace lpc {
namespace vm {

class Vm;

enum class ReloadMsgType : std::uint8_t {
    Hello = 0x01,
    HelloAck = 0x02,
    PrepareReload = 0x10,
    PrepareResult = 0x11,
    ActivateReload = 0x12,
    ActivateResult = 0x13,
    RollbackReload = 0x14,
    RollbackResult = 0x15,
    StatusQuery = 0x20,
    StatusResponse = 0x21,
    SyncState = 0x30,
    SyncAck = 0x31,
    Heartbeat = 0x40,
    Error = 0xFF,
};

struct ReloadMessage {
    ReloadMsgType type = ReloadMsgType::Error;
    std::uint32_t request_id = 0;
    std::string module_name;
    std::uint64_t version_id = 0;
    std::string chunk_data;
    std::string compat_level;
    std::string migration_data;
    std::string status_json;
    std::string error_message;
    bool success = false;
};

class ReloadProtocol {
public:
    static std::vector<std::uint8_t> Encode(const ReloadMessage &msg);
    static bool Decode(const std::uint8_t *data, std::size_t len, ReloadMessage &out_msg);

private:
    static void WriteU16(std::vector<std::uint8_t> &buf, std::uint16_t v);
    static void WriteU32(std::vector<std::uint8_t> &buf, std::uint32_t v);
    static void WriteU64(std::vector<std::uint8_t> &buf, std::uint64_t v);
    static void WriteStr(std::vector<std::uint8_t> &buf, const std::string &s);
    static bool ReadU16(const std::uint8_t *data, std::size_t len, std::size_t &pos, std::uint16_t &out);
    static bool ReadU32(const std::uint8_t *data, std::size_t len, std::size_t &pos, std::uint32_t &out);
    static bool ReadU64(const std::uint8_t *data, std::size_t len, std::size_t &pos, std::uint64_t &out);
    static bool ReadStr(const std::uint8_t *data, std::size_t len, std::size_t &pos, std::string &out);
};

struct PeerInfo {
    std::string host;
    int port = 0;
    std::uint64_t peer_id = 0;
    bool connected = false;
};

class HotReloadCoordinator {
public:
    using SendFn = std::function<void(std::uint64_t peer_id, const std::vector<std::uint8_t> &data)>;
    using BroadcastFn = std::function<void(const std::vector<std::uint8_t> &data)>;

    HotReloadCoordinator(Vm &vm, SendFn send_fn, BroadcastFn broadcast_fn);
    ~HotReloadCoordinator() = default;

    void SetLocalPeerId(std::uint64_t id) { local_peer_id_ = id; }
    std::uint64_t local_peer_id() const { return local_peer_id_; }

    void AddPeer(std::uint64_t peer_id, const std::string &host, int port);
    void RemovePeer(std::uint64_t peer_id);
    const std::unordered_map<std::uint64_t, PeerInfo> &peers() const { return peers_; }

    void OnMessage(std::uint64_t from_peer_id, const ReloadMessage &msg);

    RuntimeError PrepareAndBroadcast(const std::string &module_name,
                                      const std::string &chunk_data,
                                      const std::string &compat_level,
                                      const std::string &migration_data = "");
    RuntimeError ActivateAndBroadcast(const std::string &module_name, std::uint64_t version_id);
    RuntimeError RollbackAndBroadcast(const std::string &module_name, std::uint64_t version_id);
    void BroadcastStatus();

    void SyncStateToPeer(std::uint64_t peer_id);
    std::uint32_t next_request_id() { return ++request_id_counter_; }

private:
    Vm &vm_;
    SendFn send_fn_;
    BroadcastFn broadcast_fn_;
    std::uint64_t local_peer_id_ = 0;
    std::uint32_t request_id_counter_ = 0;
    std::unordered_map<std::uint64_t, PeerInfo> peers_;
    std::unordered_map<std::uint32_t, std::uint64_t> pending_requests_;
};

class HotReloadTcpTransport {
public:
    using OnMessageFn = std::function<void(std::uint64_t peer_id, const ReloadMessage &msg)>;

    HotReloadTcpTransport();
    ~HotReloadTcpTransport();

    bool Listen(int port);
    bool Connect(const std::string &host, int port, std::uint64_t *out_peer_id);
    void Close();
    void Poll(int timeout_ms = 10);
    void Send(std::uint64_t peer_id, const std::vector<std::uint8_t> &data);
    void Broadcast(const std::vector<std::uint8_t> &data);

    void SetOnMessage(OnMessageFn fn) { on_message_ = std::move(fn); }
    bool IsListening() const { return listen_fd_ >= 0; }
    int listen_port() const { return listen_port_; }

private:
    int listen_fd_ = -1;
    int listen_port_ = 0;
    struct PeerConn {
        int fd = -1;
        std::string host;
        int port = 0;
        std::uint64_t peer_id = 0;
        std::vector<std::uint8_t> recv_buf;
    };
    std::unordered_map<std::uint64_t, PeerConn> connections_;
    std::uint64_t next_conn_id_ = 1;
    OnMessageFn on_message_;
};

} // namespace vm
} // namespace lpc

#endif

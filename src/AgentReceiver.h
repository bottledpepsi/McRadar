// AgentReceiver.h - receives the player snapshots the Java agent sends to 127.0.0.1:<port> (UDP).
//
// Why UDP on loopback? Each datagram is one complete, atomic snapshot: no framing, no torn
// reads, no shared-memory synchronisation, and nothing breaks when either side restarts.
#pragma once
#include "Types.h"
#include <cstdint>
#include <vector>

class AgentReceiver {
public:
    explicit AgentReceiver(uint16_t port);   // WSAStartup + socket + bind (loopback only)
    ~AgentReceiver();                        // closes the socket, WSACleanup
    AgentReceiver(const AgentReceiver&) = delete;
    AgentReceiver& operator=(const AgentReceiver&) = delete;

    bool Ok() const { return sock_ != kInvalid; }

    // Waits up to timeoutMs for data, drains everything queued and keeps only the NEWEST valid
    // snapshot. Returns true if `out` was filled.
    bool Receive(WorldSnapshot& out, int timeoutMs);

    // Exposed for testing: parses one datagram (see Snapshot.java for the format).
    static bool Parse(const char* data, size_t len, WorldSnapshot& out);

private:
    static constexpr uintptr_t kInvalid = ~uintptr_t{0};
    uintptr_t sock_ = kInvalid;      // SOCKET, kept opaque so winsock2.h stays out of headers
    bool wsaStarted_ = false;
    std::vector<char> buf_;
};

#include <winsock2.h>
#include "AgentReceiver.h"
#include "Logger.h"
#include <cstdlib>
#include <sstream>
#include <string>

AgentReceiver::AgentReceiver(uint16_t port) : buf_(65536) {
    WSADATA wsa{};
    if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) { LOG_ERROR("WSAStartup failed."); return; }
    wsaStarted_ = true;

    SOCKET s = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (s == INVALID_SOCKET) { LOG_ERROR("socket() failed (%d).", WSAGetLastError()); return; }

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);   // 127.0.0.1 only: never reachable from the network
    if (bind(s, reinterpret_cast<sockaddr*>(&addr), sizeof addr) == SOCKET_ERROR) {
        LOG_ERROR("Cannot listen on 127.0.0.1:%u (error %d). Is another mcradar running? Change [agent] port.",
                  static_cast<unsigned>(port), WSAGetLastError());
        closesocket(s);
        return;
    }
    u_long nonBlocking = 1;
    ioctlsocket(s, FIONBIO, &nonBlocking);
    sock_ = static_cast<uintptr_t>(s);
    LOG_INFO("Listening for agent data on udp://127.0.0.1:%u", static_cast<unsigned>(port));
}

AgentReceiver::~AgentReceiver() {
    if (sock_ != kInvalid) closesocket(static_cast<SOCKET>(sock_));
    if (wsaStarted_) WSACleanup();
}

bool AgentReceiver::Receive(WorldSnapshot& out, int timeoutMs) {
    if (sock_ == kInvalid) { Sleep(static_cast<DWORD>(timeoutMs)); return false; }
    const SOCKET s = static_cast<SOCKET>(sock_);

    fd_set readSet;
    FD_ZERO(&readSet);
    FD_SET(s, &readSet);
    timeval tv{timeoutMs / 1000, (timeoutMs % 1000) * 1000};
    if (select(0, &readSet, nullptr, nullptr, &tv) <= 0) return false;

    bool got = false;
    for (;;) {   // drain: if we fell behind, only the newest snapshot matters
        int n = recvfrom(s, buf_.data(), static_cast<int>(buf_.size()), 0, nullptr, nullptr);
        if (n <= 0) break;
        WorldSnapshot tmp;
        if (Parse(buf_.data(), static_cast<size_t>(n), tmp)) { out = std::move(tmp); got = true; }
    }
    return got;
}

// MCRADAR 1 / S <status> / L <name> x y z yaw / P <name> x y z yaw      (yaw "-" = unknown)
bool AgentReceiver::Parse(const char* data, size_t len, WorldSnapshot& out) {
    std::istringstream in(std::string(data, len));
    std::string line;
    if (!std::getline(in, line) || line.rfind("MCRADAR 1", 0) != 0) return false;   // wrong/foreign packet

    WorldSnapshot snap;
    snap.attached = true;
    snap.status = "agent connected";
    while (std::getline(in, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.size() < 2 || line[1] != ' ') continue;
        const char tag = line[0];
        const std::string rest = line.substr(2);
        if (tag == 'S') { snap.status = rest; continue; }
        if (tag != 'L' && tag != 'P') continue;

        std::istringstream ls(rest);
        std::string name, sx, sy, sz, syaw;
        if (!(ls >> name >> sx >> sy >> sz >> syaw)) continue;
        PlayerState p;
        p.name = name;
        p.pos = {std::strtod(sx.c_str(), nullptr), std::strtod(sy.c_str(), nullptr), std::strtod(sz.c_str(), nullptr)};
        p.yawValid = syaw != "-";
        p.yawDeg = p.yawValid ? static_cast<float>(std::strtod(syaw.c_str(), nullptr)) : 0.0f;
        if (tag == 'L') { snap.local = p; snap.localValid = true; }
        else snap.others.push_back(std::move(p));
    }
    out = std::move(snap);
    return true;
}

// UdpListener.h - receives the agent's snapshots on 127.0.0.1:<port> from a background thread.
//
// Hand-off to the render thread (double buffering): the worker parses each datagram into a NEW
// immutable WorldSnapshot (the "back buffer") and publishes it by swapping a shared_ptr under a mutex
// that is held only for the pointer copy (nanoseconds, never during I/O or parsing). The render
// thread grabs the current pointer ("front buffer") and reads it without any lock; the worker can
// publish the next snapshot at any time without touching what the renderer holds.
#pragma once
#include "Types.h"
#include <atomic>
#include <chrono>
#include <cstdint>
#include <memory>
#include <mutex>
#include <thread>

class UdpListener
{
public:
    using Clock = std::chrono::steady_clock;

    explicit UdpListener(uint16_t port) : port_(port)
    {
    }

    ~UdpListener() { Stop(); }
    UdpListener(const UdpListener&) = delete;
    UdpListener& operator=(const UdpListener&) = delete;

    bool Start(); // WSAStartup + bind + spawn thread; false (and logged) if the port is taken
    void Stop();
    bool Ok() const { return running_.load(); }

    std::shared_ptr<const WorldSnapshot> Latest() const; // never blocks on I/O
    Clock::time_point LastPacketTime() const; // time_point{} if nothing yet
    uint64_t PacketCount() const { return count_.load(); }

private:
    void Loop(std::stop_token st);

    uint16_t port_;
    uintptr_t sock_ = ~uintptr_t{0};
    bool wsa_ = false;
    std::atomic<bool> running_{false};
    std::atomic<uint64_t> count_{0};
    std::atomic<int64_t> lastNs_{0};
    mutable std::mutex mtx_;
    std::shared_ptr<const WorldSnapshot> latest_;
    std::jthread thread_;
};

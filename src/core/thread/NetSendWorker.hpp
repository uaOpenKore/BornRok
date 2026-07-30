#pragma once
#include <functional>
#include <thread>
#include <utility>
#include <vector>

#include "core/Types.hpp"
#include "core/thread/ThreadRole.hpp"
#include "core/thread/ThreadSafeQueue.hpp"

namespace uaro {

// The net-send thread of the planned 4-thread split (ThreadRole::NetSend, an
// efficiency core). The logic thread hands finished outgoing packets to enqueue();
// this worker drains them to a sink (the socket) off the hot path, so a blocking
// send can never stall simulation or rendering.
//
// The sink is injected (a callable, not a hard Socket dependency) so the worker is
// unit-testable with an in-memory sink and so the same worker can drive any
// transport. The sink returns false once the link is gone; the worker then stops
// sending but keeps draining the queue (so producers never block) until stop().
//
// This is Phase 1: a self-contained, tested component. Wiring Connection's send path
// through it (instead of a direct socket write on the game thread) is the next step.
class NetSendWorker {
public:
    using Sink = std::function<bool(const std::vector<u8>&)>;

    explicit NetSendWorker(Sink sink) : sink_(std::move(sink)) {}
    ~NetSendWorker() { stop(); }

    NetSendWorker(const NetSendWorker&) = delete;
    NetSendWorker& operator=(const NetSendWorker&) = delete;

    // Spawn the worker thread. Idempotent.
    void start() {
        if (running_) return;
        running_ = true;
        thread_ = std::thread([this] {
            configureCurrentThread(ThreadRole::NetSend, "net-send");
            while (auto pkt = queue_.waitPop()) {
                // Once the link drops, keep popping (so producers never block on a full
                // queue) but stop hitting a dead socket.
                if (alive_ && !sink_(*pkt)) alive_ = false;
            }
        });
    }

    // Hand a finished outgoing packet to the worker. Thread-safe; never blocks.
    void enqueue(std::vector<u8> packet) { queue_.push(std::move(packet)); }

    // Close the queue (the worker drains what remains) and join. Idempotent.
    void stop() {
        if (!running_) return;
        running_ = false;
        queue_.close();
        if (thread_.joinable()) thread_.join();
    }

    usize pending() const { return queue_.size(); }

private:
    Sink sink_;
    ThreadSafeQueue<std::vector<u8>> queue_;
    std::thread thread_;
    bool running_ = false;  // touched only by start()/stop() (the owning thread)
    bool alive_ = true;     // touched only by the worker thread
};

} // namespace uaro

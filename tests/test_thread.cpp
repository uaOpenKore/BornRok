// Unit tests for the thread primitives that the planned worker threads build on.
// Bounded by construction (producers push a fixed count, then close; consumers
// drain to nullopt), so these can never hang.
#include "microtest.hpp"

#include <atomic>
#include <mutex>
#include <thread>
#include <vector>

#include "core/thread/NetSendWorker.hpp"
#include "core/thread/SnapshotMailbox.hpp"
#include "core/thread/ThreadRole.hpp"
#include "core/thread/ThreadSafeQueue.hpp"

using namespace uaro;

TEST_CASE(queue_basic_fifo) {
    ThreadSafeQueue<int> q;
    CHECK(!q.tryPop().has_value());  // empty
    q.push(1);
    q.push(2);
    CHECK_EQ(q.size(), 2u);
    CHECK_EQ(q.tryPop().value(), 1);  // FIFO order
    CHECK_EQ(q.tryPop().value(), 2);
    CHECK(!q.tryPop().has_value());
}

TEST_CASE(queue_close_drains_then_stops) {
    ThreadSafeQueue<int> q;
    q.push(7);
    q.close();
    CHECK(q.closed());
    CHECK_EQ(q.waitPop().value(), 7);  // closed but still drains the backlog
    CHECK(!q.waitPop().has_value());   // closed and empty -> nullopt (does not block)
}

TEST_CASE(queue_producer_consumer) {
    ThreadSafeQueue<int> q;
    constexpr int kN = 10000;
    std::thread producer([&] {
        for (int i = 0; i < kN; ++i) q.push(i);
        q.close();
    });
    long long sum = 0;
    int count = 0;
    while (auto v = q.waitPop()) {
        sum += *v;
        ++count;
    }
    producer.join();
    CHECK_EQ(count, kN);
    CHECK_EQ(sum, static_cast<long long>(kN) * (kN - 1) / 2);  // 0+1+...+(N-1)
}

TEST_CASE(thread_role_configure_is_safe) {
    // Best-effort affinity/QoS/naming must never throw or crash, including on a
    // uniform host where there is nothing to pin.
    configureCurrentThread(ThreadRole::Logic, "uaro-test");
    CHECK(true);
}

TEST_CASE(netsend_worker_drains_in_order) {
    // The worker hands every enqueued packet to the sink, in FIFO order, and stop()
    // drains what is queued before joining.
    std::mutex m;
    std::vector<std::vector<u8>> got;
    {
        NetSendWorker w([&](const std::vector<u8>& p) {
            std::lock_guard<std::mutex> lk(m);
            got.push_back(p);
            return true;
        });
        w.start();
        for (u8 i = 0; i < 5; ++i) w.enqueue(std::vector<u8>{i, static_cast<u8>(i + 10)});
        w.stop();  // close + drain + join
    }
    CHECK_EQ(got.size(), 5u);
    for (u8 i = 0; i < 5; ++i) {
        CHECK_EQ(got[i][0], i);
        CHECK_EQ(got[i][1], static_cast<u8>(i + 10));
    }
}

TEST_CASE(netsend_worker_stops_sending_after_link_drop) {
    // Once the sink reports the link is gone (false), the worker keeps draining the
    // queue but makes no further sink calls.
    std::atomic<int> calls{0};
    {
        NetSendWorker w([&](const std::vector<u8>&) {
            calls++;
            return false;  // link gone from the first send
        });
        w.start();
        for (int i = 0; i < 4; ++i) w.enqueue(std::vector<u8>{0});
        w.stop();
    }
    CHECK_EQ(calls.load(), 1);  // only the first packet reached the sink
}

TEST_CASE(snapshot_mailbox_consume_tracks_version) {
    SnapshotMailbox<int> mb;
    int out = -1;
    u64 seen = 0;
    CHECK(!mb.consume(out, seen));  // nothing published yet
    CHECK_EQ(out, -1);
    mb.publish(42);
    CHECK(mb.consume(out, seen));
    CHECK_EQ(out, 42);
    CHECK_EQ(seen, 1u);
    CHECK(!mb.consume(out, seen));  // no new publish -> false, keep previous
    CHECK_EQ(out, 42);
    mb.publish(7);
    mb.publish(9);  // overwrites the unconsumed 7
    CHECK(mb.consume(out, seen));
    CHECK_EQ(out, 9);  // only the latest snapshot survives
    CHECK_EQ(seen, 3u);
}

TEST_CASE(snapshot_mailbox_threaded_latest_wins) {
    // A producer publishes a monotonically increasing sequence; the consumer never
    // sees a value go backwards and ends up observing the final one.
    SnapshotMailbox<int> mb;
    std::atomic<bool> done{false};
    std::thread producer([&] {
        for (int i = 1; i <= 1000; ++i) mb.publish(i);
        done = true;
    });
    int out = 0, last = 0;
    u64 seen = 0;
    while (!done || mb.version() != seen) {
        if (mb.consume(out, seen)) {
            CHECK(out >= last);  // versions only move forward, so values never regress
            last = out;
        }
    }
    producer.join();
    CHECK_EQ(out, 1000);  // the final published snapshot is observable
}

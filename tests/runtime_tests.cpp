// Runtime behavior tests for the sidecar IPC host:
// - outbound wakeup (frames must not wait for the poll timeout)
// - bounded write deadline against a stalled peer
// - bounded send queue with shed policy (response frames never shed)
// - stop() interrupting a blocking poll
#include "phi/adapter/sdk/sidecar.h"
#include "test_support.h"

#include <atomic>
#include <chrono>
#include <cstdio>
#include <thread>

namespace sdk = phicore::adapter::sdk;
namespace v1 = phicore::adapter::v1;
using phitest::TestClient;
using Clock = std::chrono::steady_clock;

namespace {

// Documented cap of the outbound send queue (see README "Outbound send path").
constexpr std::size_t kDocumentedQueueMaxDepth = 4096;

void testWakeupLatency()
{
    const std::string path = phitest::uniqueSocketPath("wakeup");
    sdk::SidecarDispatcher dispatcher(path);
    std::atomic_bool connected{false};
    sdk::SidecarHandlers handlers;
    handlers.onConnected = [&connected]() { connected.store(true); };
    dispatcher.setHandlers(std::move(handlers));
    v1::Utf8String err;
    REQUIRE(dispatcher.start(&err));

    std::atomic_bool run{true};
    std::thread poller([&]() {
        while (run.load())
            dispatcher.pollOnce(std::chrono::milliseconds(2000), nullptr);
    });

    TestClient client;
    REQUIRE(client.connectTo(path));
    const auto connectDeadline = Clock::now() + std::chrono::seconds(5);
    while (!connected.load() && Clock::now() < connectDeadline)
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    REQUIRE(connected.load());
    // Let the poll thread re-enter epoll_wait with the full 2s timeout.
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    const auto t0 = Clock::now();
    CHECK(dispatcher.sendConnectionStateChanged("test-instance", true, nullptr));

    v1::FrameHeader header{};
    std::string payload;
    const bool got = client.readFrame(1500, &header, &payload);
    const long latencyMs = phitest::msSince(t0);
    CHECK_MSG(got, "no frame within 1500ms");
    // Far below the 2000ms poll timeout proves the wakeup works.
    CHECK_MSG(latencyMs < 500, "latency=%ldms", latencyMs);
    if (got) {
        CHECK(v1::isValidFrameHeader(header));
        CHECK(phitest::contains(payload, "\"connected\":true"));
    }
    std::printf("wakeup latency: %ldms (poll timeout 2000ms)\n", latencyMs);

    run.store(false);
    dispatcher.stop();
    poller.join();
}

void testWriteDeadlineOnStalledPeer()
{
    const std::string path = phitest::uniqueSocketPath("stall");
    sdk::SidecarDispatcher dispatcher(path);
    std::atomic_bool connected{false};
    std::atomic_bool disconnected{false};
    sdk::SidecarHandlers handlers;
    handlers.onConnected = [&connected]() { connected.store(true); };
    handlers.onDisconnected = [&disconnected]() { disconnected.store(true); };
    dispatcher.setHandlers(std::move(handlers));
    v1::Utf8String err;
    REQUIRE(dispatcher.start(&err));

    std::atomic_bool run{true};
    std::thread poller([&]() {
        while (run.load())
            dispatcher.pollOnce(std::chrono::milliseconds(100), nullptr);
    });

    TestClient client; // connects but never reads
    REQUIRE(client.connectTo(path));
    const auto connectDeadline = Clock::now() + std::chrono::seconds(5);
    while (!connected.load() && Clock::now() < connectDeadline)
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    REQUIRE(connected.load());

    // Queue far more data than the socket buffers can hold (~200 x 64KB).
    const auto t0 = Clock::now();
    const v1::Utf8String big(64 * 1024, 'x');
    for (int i = 0; i < 200; ++i)
        dispatcher.sendAdapterMetaUpdated("inst", "{\"blob\":\"" + big + "\"}", nullptr);

    while (!disconnected.load() && phitest::msSince(t0) < 20000)
        std::this_thread::sleep_for(std::chrono::milliseconds(20));

    const long tookMs = phitest::msSince(t0);
    CHECK_MSG(disconnected.load(), "stalled peer not disconnected after %ldms", tookMs);
    // The per-frame write deadline is 5s; the disconnect must happen shortly
    // after and must not be trivially early.
    CHECK_MSG(tookMs >= 4000 && tookMs < 20000, "took=%ldms", tookMs);
    std::printf("stalled peer disconnected after %ldms (deadline 5000ms)\n", tookMs);

    run.store(false);
    dispatcher.stop();
    poller.join();
}

void testQueueCapShedsOldestLogFrames()
{
    const std::string path = phitest::uniqueSocketPath("cap");
    sdk::SidecarDispatcher dispatcher(path);
    std::atomic_bool connected{false};
    sdk::SidecarHandlers handlers;
    handlers.onConnected = [&connected]() { connected.store(true); };
    dispatcher.setHandlers(std::move(handlers));
    v1::Utf8String err;
    REQUIRE(dispatcher.start(&err));

    // Single-threaded accept: poll from this thread until the client is in.
    TestClient client;
    REQUIRE(client.connectTo(path));
    const auto connectDeadline = Clock::now() + std::chrono::seconds(5);
    while (!connected.load() && Clock::now() < connectDeadline)
        dispatcher.pollOnce(std::chrono::milliseconds(10), nullptr);
    REQUIRE(connected.load());

    // Queue 5000 log frames WITHOUT polling in between: no flush runs, so the
    // shed policy caps the queue deterministically at the documented depth.
    sdk::LogEntry entry;
    entry.level = sdk::LogLevel::Info;
    entry.message = "flood";
    bool allAccepted = true;
    for (int i = 0; i < 5000; ++i) {
        if (!dispatcher.sendLog("inst", "test", entry, nullptr))
            allAccepted = false;
    }
    CHECK(allAccepted); // shed drops the oldest queued frame, never the new one

    // Drain: reader thread counts complete frames while this thread flushes.
    std::atomic_bool readerRun{true};
    std::atomic<std::size_t> frameCount{0};
    std::thread reader([&]() {
        v1::FrameHeader header{};
        std::string payload;
        while (readerRun.load()) {
            if (client.readFrame(100, &header, &payload))
                frameCount.fetch_add(1);
        }
    });

    std::size_t lastCount = 0;
    auto lastProgress = Clock::now();
    while (phitest::msSince(lastProgress) < 1000) {
        dispatcher.pollOnce(std::chrono::milliseconds(10), nullptr);
        const std::size_t current = frameCount.load();
        if (current != lastCount) {
            lastCount = current;
            lastProgress = Clock::now();
        }
    }
    readerRun.store(false);
    reader.join();

    CHECK_MSG(frameCount.load() == kDocumentedQueueMaxDepth,
              "received=%zu expected=%zu", frameCount.load(), kDocumentedQueueMaxDepth);
    std::printf("queue cap: queued 5000, received %zu (cap %zu)\n",
                frameCount.load(), kDocumentedQueueMaxDepth);

    dispatcher.stop();
}

void testStopInterruptsBlockingPoll()
{
    const std::string path = phitest::uniqueSocketPath("stop");
    sdk::SidecarDispatcher dispatcher(path);
    v1::Utf8String err;
    REQUIRE(dispatcher.start(&err));

    std::thread poller([&]() {
        dispatcher.pollOnce(std::chrono::milliseconds(5000), nullptr);
    });
    std::this_thread::sleep_for(std::chrono::milliseconds(200)); // let it block

    const auto t0 = Clock::now();
    dispatcher.stop();
    const long stopMs = phitest::msSince(t0);
    poller.join();
    CHECK_MSG(stopMs < 1000, "stop() took %ldms against a 5000ms poll", stopMs);
    std::printf("stop() returned in %ldms while poll timeout was 5000ms\n", stopMs);
}

} // namespace

int main()
{
    testWakeupLatency();
    testWriteDeadlineOnStalledPeer();
    testQueueCapShedsOldestLogFrames();
    testStopInterruptsBlockingPoll();

    if (phitest::g_failures == 0) {
        std::printf("runtime_tests: all passed\n");
        return 0;
    }
    std::printf("runtime_tests: %d failure(s)\n", phitest::g_failures);
    return 1;
}

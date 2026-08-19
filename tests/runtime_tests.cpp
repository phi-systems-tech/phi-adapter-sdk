// Runtime behavior tests for the sidecar IPC host:
// - outbound wakeup (frames must not wait for the poll timeout)
// - bounded write deadline against a stalled peer
// - bounded send queue with shed policy (response frames never shed)
// - stop() interrupting a blocking poll
// - factory execution backend: blocking factory hooks must not stall the poll
//   loop, and the default (no backend) must stay inline
#include "phi/adapter/sdk/sidecar.h"
#include "test_support.h"

#include <atomic>
#include <chrono>
#include <cstdio>
#include <memory>
#include <string>
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

// A factory whose action handler blocks, opted into a factory execution
// backend. Reuses the SDK's default (thread-based) backend implementation via
// the instance-backend hook, so the test stays Qt-free.
class BlockingProbeFactory final : public sdk::AdapterFactory
{
public:
    std::atomic<int> probeCalls{0};
    std::atomic_bool probeRunning{false};
    std::atomic_bool stopped{false};
    std::thread::id hookThread{};
    std::thread::id stoppingThread{};
    static constexpr auto kProbeDuration = std::chrono::milliseconds(600);

protected:
    v1::Utf8String pluginType() const override { return "test.blocking.probe"; }

    std::unique_ptr<sdk::InstanceExecutionBackend> createFactoryExecutionBackend() override
    {
        return createInstanceExecutionBackend({});
    }

    std::unique_ptr<sdk::AdapterInstance> createInstance(const v1::ExternalId &) override
    {
        return nullptr;
    }

    void onFactoryStopping() override
    {
        stoppingThread = std::this_thread::get_id();
        stopped.store(true);
    }

    void onFactoryActionInvoke(const sdk::AdapterActionInvokeRequest &request) override
    {
        hookThread = std::this_thread::get_id();
        probeRunning.store(true);
        probeCalls.fetch_add(1);
        std::this_thread::sleep_for(kProbeDuration);
        probeRunning.store(false);

        v1::ActionResponse response;
        response.id = request.cmdId;
        response.status = v1::CmdStatus::Success;
        response.resultType = v1::ActionResultType::String;
        response.resultValue = "probe-done";
        v1::Utf8String error;
        sendResult(response, &error);
    }
};

void testFactoryBackendKeepsPollResponsive()
{
    const std::string path = phitest::uniqueSocketPath("factorybackend");
    auto factory = std::make_unique<BlockingProbeFactory>();
    BlockingProbeFactory *factoryPtr = factory.get();
    sdk::SidecarHost host(path, std::move(factory));
    v1::Utf8String err;
    REQUIRE(host.start(&err));

    TestClient client;
    REQUIRE(client.connectTo(path));

    // Bootstrap: the descriptor reply must come back even though it is produced
    // while the factory hook is queued on its own thread.
    const std::string bootstrap = "{\"command\":257,\"cmdId\":1,\"payload\":{"
                                  "\"adapterId\":1,\"pluginType\":\"test.blocking.probe\","
                                  "\"externalId\":\"\",\"staticConfig\":{}}}";
    REQUIRE(client.sendFrame(v1::MessageType::Request, 1, bootstrap));

    v1::FrameHeader header{};
    std::string payload;
    bool gotDescriptor = false;
    auto deadline = Clock::now() + std::chrono::seconds(3);
    while (!gotDescriptor && Clock::now() < deadline) {
        host.pollOnce(std::chrono::milliseconds(10), nullptr);
        gotDescriptor = client.readFrame(10, &header, &payload);
    }
    REQUIRE(gotDescriptor);
    CHECK(phitest::contains(payload, "test.blocking.probe"));

    // Factory action: 0x0202 = CmdAdapterActionInvoke, factory scope.
    const std::string action = "{\"command\":514,\"cmdId\":2,\"payload\":{"
                               "\"externalId\":\"\",\"actionId\":\"probe\",\"params\":{}}}";
    const auto t0 = Clock::now();
    REQUIRE(client.sendFrame(v1::MessageType::Request, 2, action));

    // Wait until the blocking hook is actually running on the backend thread.
    deadline = Clock::now() + std::chrono::seconds(2);
    while (!factoryPtr->probeRunning.load() && Clock::now() < deadline)
        host.pollOnce(std::chrono::milliseconds(5), nullptr);
    REQUIRE(factoryPtr->probeRunning.load());

    // While it runs, the poll loop must still move frames. sendConnectionStateChanged
    // travels the same path a device event would.
    CHECK(host.dispatcher()->sendConnectionStateChanged("inst-1", true, nullptr));
    bool gotEvent = false;
    deadline = Clock::now() + std::chrono::milliseconds(400);
    while (!gotEvent && Clock::now() < deadline) {
        host.pollOnce(std::chrono::milliseconds(5), nullptr);
        gotEvent = client.readFrame(5, &header, &payload);
    }
    const long eventMs = phitest::msSince(t0);
    CHECK_MSG(gotEvent, "no event frame while factory hook was blocking (%ldms)", eventMs);
    CHECK_MSG(factoryPtr->probeRunning.load(), "hook finished too early to prove anything");
    CHECK(phitest::contains(payload, "\"connected\":true"));

    // And the deferred action result still arrives once the hook completes.
    bool gotResult = false;
    deadline = Clock::now() + std::chrono::seconds(3);
    while (!gotResult && Clock::now() < deadline) {
        host.pollOnce(std::chrono::milliseconds(10), nullptr);
        if (client.readFrame(10, &header, &payload))
            gotResult = phitest::contains(payload, "probe-done");
    }
    CHECK_MSG(gotResult, "no factory action result after %ldms", phitest::msSince(t0));
    CHECK(factoryPtr->probeCalls.load() == 1);
    std::printf("factory backend: event delivered after %ldms while a %lldms hook was blocking\n",
                eventMs,
                static_cast<long long>(BlockingProbeFactory::kProbeDuration.count()));

    host.stop();
    CHECK_MSG(factoryPtr->stopped.load(), "onFactoryStopping was not called");
    CHECK_MSG(factoryPtr->stoppingThread == factoryPtr->hookThread,
              "onFactoryStopping must run on the factory backend thread");
    CHECK_MSG(factoryPtr->hookThread != std::this_thread::get_id(),
              "factory hook ran on the polling thread despite a backend");
}

void testFactoryBackendDefaultsToInline()
{
    // Without an override the factory hook must still run - on the poll thread.
    class InlineFactory final : public sdk::AdapterFactory
    {
    public:
        std::atomic_bool bootstrapped{false};
        std::thread::id hookThread{};

    protected:
        v1::Utf8String pluginType() const override { return "test.inline"; }
        std::unique_ptr<sdk::AdapterInstance> createInstance(const v1::ExternalId &) override
        {
            return nullptr;
        }
        void onBootstrap(const sdk::BootstrapRequest &) override
        {
            hookThread = std::this_thread::get_id();
            bootstrapped.store(true);
        }
    };

    const std::string path = phitest::uniqueSocketPath("factoryinline");
    auto factory = std::make_unique<InlineFactory>();
    InlineFactory *factoryPtr = factory.get();
    sdk::SidecarHost host(path, std::move(factory));
    v1::Utf8String err;
    REQUIRE(host.start(&err));

    TestClient client;
    REQUIRE(client.connectTo(path));
    const std::string bootstrap = "{\"command\":257,\"cmdId\":1,\"payload\":{"
                                  "\"adapterId\":1,\"pluginType\":\"test.inline\","
                                  "\"externalId\":\"\",\"staticConfig\":{}}}";
    REQUIRE(client.sendFrame(v1::MessageType::Request, 1, bootstrap));

    const auto deadline = Clock::now() + std::chrono::seconds(3);
    while (!factoryPtr->bootstrapped.load() && Clock::now() < deadline)
        host.pollOnce(std::chrono::milliseconds(10), nullptr);
    REQUIRE(factoryPtr->bootstrapped.load());
    CHECK_MSG(factoryPtr->hookThread == std::this_thread::get_id(),
              "default (null) factory backend must keep hooks on the polling thread");

    host.stop();
}

// Instances whose stop() blocks past the shutdown budget: the host must bound
// the teardown regardless, and must not spend the full budget per instance.
class BlockingStopInstance final : public sdk::AdapterInstance
{
public:
    static constexpr auto kStopBlockFor = std::chrono::seconds(5);

protected:
    bool start() override { return true; }

    void stop() override
    {
        // Stands in for the uncancellable blocking waits F-33 is about.
        std::this_thread::sleep_for(kStopBlockFor);
    }
};

class BlockingStopFactory final : public sdk::AdapterFactory
{
protected:
    v1::Utf8String pluginType() const override { return "test.blocking.stop"; }
    std::unique_ptr<sdk::AdapterInstance> createInstance(const v1::ExternalId &) override
    {
        return std::make_unique<BlockingStopInstance>();
    }
};

// Mirrors what an adapter with chunked blocking I/O does: sit in short waits and
// poll stopRequested() between them. Such a thread cannot run queued work, so the
// flag is the only way it can learn about the shutdown.
class CooperativeInstance final : public sdk::AdapterInstance
{
public:
    std::atomic_bool inWork{false};
    std::atomic_bool sawStopRequest{false};

protected:
    bool start() override { return true; }

    // Runs on this instance's execution backend, so while it is in here the
    // backend cannot run the queued hostStop() at all.
    void onConfigChanged(const sdk::ConfigChangedRequest &request) override
    {
        sdk::AdapterInstance::onConfigChanged(request);
        inWork.store(true);
        for (int i = 0; i < 100; ++i) { // 100 x 100ms = 10s if never interrupted
            if (stopRequested()) {
                sawStopRequest.store(true);
                break;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
        inWork.store(false);
    }
};

class CooperativeFactory final : public sdk::AdapterFactory
{
public:
    CooperativeInstance *created = nullptr;

protected:
    v1::Utf8String pluginType() const override { return "test.cooperative"; }
    std::unique_ptr<sdk::AdapterInstance> createInstance(const v1::ExternalId &) override
    {
        auto instance = std::make_unique<CooperativeInstance>();
        created = instance.get();
        return instance;
    }
};

void testStopRequestReachesBlockedInstance()
{
    const std::string path = phitest::uniqueSocketPath("stopflag");
    auto factory = std::make_unique<CooperativeFactory>();
    CooperativeFactory *factoryPtr = factory.get();
    sdk::SidecarHost host(path, std::move(factory));
    v1::Utf8String err;
    REQUIRE(host.start(&err));

    TestClient client;
    REQUIRE(client.connectTo(path));
    const std::string config = "{\"command\":258,\"cmdId\":1,\"payload\":{"
                               "\"adapterId\":1,\"pluginType\":\"test.cooperative\","
                               "\"externalId\":\"inst-1\",\"enabled\":true}}";
    REQUIRE(client.sendFrame(v1::MessageType::Request, 1, config));
    auto deadline = Clock::now() + std::chrono::seconds(3);
    while (host.instance("inst-1") == nullptr && Clock::now() < deadline)
        host.pollOnce(std::chrono::milliseconds(10), nullptr);
    REQUIRE(host.instance("inst-1") != nullptr);
    REQUIRE(factoryPtr->created != nullptr);

    // The config handler above now occupies the instance's execution thread.
    deadline = Clock::now() + std::chrono::seconds(2);
    while (!factoryPtr->created->inWork.load() && Clock::now() < deadline)
        host.pollOnce(std::chrono::milliseconds(5), nullptr);
    REQUIRE(factoryPtr->created->inWork.load());

    CooperativeInstance *instance = factoryPtr->created;
    const auto t0 = Clock::now();
    host.stop();
    const long tookMs = phitest::msSince(t0);

    CHECK_MSG(instance->sawStopRequest.load(), "blocked instance never observed stopRequested()");
    CHECK_MSG(tookMs < 1000, "cooperative unwind took %ldms", tookMs);
    std::printf("stop request: blocked instance unwound in %ldms instead of 10000ms\n", tookMs);
}

void testShutdownBudgetIsShared()
{
    const std::string path = phitest::uniqueSocketPath("stopbudget");
    sdk::SidecarHost host(path, std::make_unique<BlockingStopFactory>());
    v1::Utf8String err;
    REQUIRE(host.start(&err));

    TestClient client;
    REQUIRE(client.connectTo(path));

    // Two instances, created the way core does it: config.changed per externalId.
    for (const char *externalId : {"inst-a", "inst-b"}) {
        const std::string config = std::string("{\"command\":258,\"cmdId\":1,\"payload\":{"
                                               "\"adapterId\":1,\"pluginType\":\"test.blocking.stop\","
                                               "\"externalId\":\"")
            + externalId + "\",\"enabled\":true}}";
        REQUIRE(client.sendFrame(v1::MessageType::Request, 1, config));
        const auto deadline = Clock::now() + std::chrono::seconds(3);
        while (host.instance(externalId) == nullptr && Clock::now() < deadline)
            host.pollOnce(std::chrono::milliseconds(10), nullptr);
        REQUIRE(host.instance(externalId) != nullptr);
    }

    const auto t0 = Clock::now();
    host.stop();
    const long tookMs = phitest::msSince(t0);

    // Both instances block for 5s each; the budget must still hold, with room
    // for the forced joins on top of it.
    const long budgetMs = static_cast<long>(sdk::kShutdownBudget.count());
    CHECK_MSG(tookMs < budgetMs + 1500, "stop() took %ldms for a %ldms budget", tookMs, budgetMs);
    CHECK_MSG(tookMs >= budgetMs / 2, "stop() returned in %ldms - budget not honoured at all", tookMs);
    std::printf("shutdown budget: two instances blocking 5s each stopped in %ldms (budget %ldms)\n",
                tookMs, budgetMs);
}

} // namespace

int main()
{
    testWakeupLatency();
    testWriteDeadlineOnStalledPeer();
    testQueueCapShedsOldestLogFrames();
    testStopInterruptsBlockingPoll();
    testFactoryBackendKeepsPollResponsive();
    testFactoryBackendDefaultsToInline();
    testShutdownBudgetIsShared();
    testStopRequestReachesBlockedInstance();

    if (phitest::g_failures == 0) {
        std::printf("runtime_tests: all passed\n");
        return 0;
    }
    std::printf("runtime_tests: %d failure(s)\n", phitest::g_failures);
    return 1;
}

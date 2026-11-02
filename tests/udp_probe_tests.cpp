// UdpProbe against a stub UDP socket on loopback: the payload arrives, the
// reply comes back to the probe's ephemeral port, the done callback follows
// after the window, and a silent target just ends the run after the window.

#include <phi/adapter/testing/check.h>

#include "phi/adapter/net/udp_probe.h"
#include "phi/runtime/epollloop.h"

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include <atomic>
#include <chrono>
#include <string>
#include <thread>
#include <vector>

using namespace phicore::adapter::net;
using namespace std::chrono_literals;

namespace {

struct Stub {
    int fd = -1;
    std::uint16_t port = 0;
    std::atomic<bool> stopping{false};
    std::string received;
    std::string answer;   // empty: stay silent
    std::thread thread;

    explicit Stub(std::string reply) : answer(std::move(reply))
    {
        fd = ::socket(AF_INET, SOCK_DGRAM | SOCK_CLOEXEC, 0);
        ::sockaddr_in a{};
        a.sin_family = AF_INET;
        a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        ::bind(fd, reinterpret_cast<::sockaddr *>(&a), sizeof(a));
        ::socklen_t len = sizeof(a);
        ::getsockname(fd, reinterpret_cast<::sockaddr *>(&a), &len);
        port = ntohs(a.sin_port);
        timeval tv{0, 100000};
        ::setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
        thread = std::thread([this]() {
            while (!stopping) {
                char buf[2048];
                ::sockaddr_in from{};
                ::socklen_t flen = sizeof(from);
                const ssize_t n = ::recvfrom(fd, buf, sizeof(buf), 0, reinterpret_cast<::sockaddr *>(&from), &flen);
                if (n < 0)
                    continue;
                received.assign(buf, static_cast<std::size_t>(n));
                if (!answer.empty())
                    ::sendto(fd, answer.data(), answer.size(), 0, reinterpret_cast<::sockaddr *>(&from), flen);
            }
        });
    }
    ~Stub()
    {
        stopping = true;
        thread.join();
        ::close(fd);
    }
};

struct Outcome {
    std::vector<std::string> replies;
    std::string fromIp;
    std::uint16_t fromPort = 0;
    bool done = false;
    std::string error = "unset";
    std::chrono::milliseconds elapsed{0};
};

Outcome run(const std::string &target, std::uint16_t port, const std::string &payload, std::chrono::milliseconds window)
{
    phi::runtime::EpollLoop loop;
    UdpProbe probe(loop);
    Outcome out;
    const auto begin = std::chrono::steady_clock::now();
    const bool started = probe.start(target, port, payload, window,
        [&](const std::string &ip, std::uint16_t p, const std::string &bytes) {
            out.fromIp = ip;
            out.fromPort = p;
            out.replies.push_back(bytes);
        },
        [&](std::string error) {
            out.done = true;
            out.error = std::move(error);
            out.elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - begin);
            loop.stop();
        });
    PHI_CHECK(started);
    PHI_CHECK(probe.running() || out.done);
    phi::runtime::Timer watchdog = loop.timerAfter(8s, [&loop]() { loop.stop(); });
    loop.run();
    PHI_CHECK(!probe.running());
    return out;
}

} // namespace

int main()
{
    {
        Stub stub("pong");
        const Outcome o = run("127.0.0.1", stub.port, "ping", 300ms);
        PHI_CHECK(stub.received == "ping");
        PHI_CHECK(o.replies.size() == 1 && o.replies[0] == "pong");
        PHI_CHECK(o.fromIp == "127.0.0.1" && o.fromPort == stub.port);
        PHI_CHECK(o.done && o.error.empty());
        PHI_CHECK(o.elapsed >= 250ms);   // the window is listened out, not cut short by the reply
    }
    {
        Stub stub("");
        const Outcome o = run("localhost", stub.port, "ping", 200ms);
        PHI_CHECK(stub.received == "ping");
        PHI_CHECK(o.replies.empty());
        PHI_CHECK(o.done && o.error.empty());
        PHI_CHECK(o.elapsed >= 150ms && o.elapsed < 3s);
    }
    {
        const Outcome o = run("no-such-host.invalid", 9, "x", 100ms);
        PHI_CHECK(o.done && !o.error.empty());
    }
    {
        // cancel(): no done callback, and the probe can run again.
        phi::runtime::EpollLoop loop;
        UdpProbe probe(loop);
        bool done = false;
        PHI_CHECK(probe.start("127.0.0.1", 9, "x", 100ms, nullptr, [&](std::string) { done = true; }));
        PHI_CHECK(probe.running());
        PHI_CHECK(!probe.start("127.0.0.1", 9, "x", 100ms, nullptr, nullptr));
        probe.cancel();
        PHI_CHECK(!probe.running() && !done);
        PHI_CHECK(probe.start("127.0.0.1", 9, "x", 50ms, nullptr, [&](std::string) { done = true; loop.stop(); }));
        phi::runtime::Timer watchdog = loop.timerAfter(8s, [&loop]() { loop.stop(); });
        loop.run();
        PHI_CHECK(done);
    }
    return phi::testing::report("sdk_udp_probe_tests");
}

#pragma once
// A one-shot UDP search on a phi::runtime loop, the building block of factory
// discovery (phi/adapter/v1/discovery.h): send one datagram to the limited
// broadcast address or to one host, collect every reply for a window, then
// say done. Nothing here knows a protocol; the payload and the reading of the
// replies are the caller's (see tdp.h for TP-Link's).
//
// The socket lives only inside a run: it is opened by start() and closed
// before the done callback, as the factory contract asks.

#include <chrono>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>

namespace phi::runtime {
class Loop;
}

namespace phicore::adapter::net {

class UdpProbe
{
public:
    /// One datagram: the sender's address and port, and its bytes.
    using OnReply = std::function<void(const std::string &fromIp, std::uint16_t fromPort, const std::string &bytes)>;
    /// The run is over: empty `error` when the window ran out, otherwise why not.
    using OnDone = std::function<void(std::string error)>;

    explicit UdpProbe(phi::runtime::Loop &loop);
    ~UdpProbe();
    UdpProbe(const UdpProbe &) = delete;
    UdpProbe &operator=(const UdpProbe &) = delete;

    /**
     * @brief Sends `payload` to `target`:`port` and listens for `window`.
     *
     * `target` empty means the limited broadcast 255.255.255.255; otherwise
     * a host name or an IPv4 address (IPv4 only). The socket is bound to an
     * ephemeral port, so replies come back to it. Replies arrive on the loop
     * through `onReply`; `onDone` follows when the window is over.
     *
     * False when a run is already going. A failure to open, resolve or send
     * ends the run at once: `onDone` is called with the reason before start()
     * returns true.
     */
    bool start(const std::string &target, std::uint16_t port, std::string payload,
               std::chrono::milliseconds window, OnReply onReply, OnDone onDone);

    /// Closes the socket and drops the run; `onDone` is not called.
    void cancel();

    [[nodiscard]] bool running() const;

private:
    struct Private;
    std::unique_ptr<Private> d;
};

} // namespace phicore::adapter::net

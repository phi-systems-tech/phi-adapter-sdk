#pragma once
// The small HTTP types an adapter hands to the clients: a header pair and the
// credentials a device asks for. Credentials is behind a d-pointer like every
// type in the boundary that may grow (a bearer token is the obvious next field).

#include <memory>
#include <string>
#include <utility>

namespace phicore::adapter::net {

using Header = std::pair<std::string, std::string>;

/// What a device asks for on a 401: answered with Basic or Digest, whichever
/// the challenge names.
class Credentials
{
public:
    Credentials();
    Credentials(std::string user, std::string password);
    Credentials(const Credentials &other);
    Credentials(Credentials &&other) noexcept;
    Credentials &operator=(const Credentials &other);
    Credentials &operator=(Credentials &&other) noexcept;
    ~Credentials();

    const std::string &user() const noexcept;
    Credentials &setUser(std::string user);
    const std::string &password() const noexcept;
    Credentials &setPassword(std::string password);
    /// No user and no password: nothing to answer a challenge with.
    bool empty() const noexcept;

private:
    struct Private;
    const Private &p() const noexcept;
    Private &m();
    std::unique_ptr<Private> d;
};

} // namespace phicore::adapter::net

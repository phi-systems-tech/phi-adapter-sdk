// The network addresses a device carries (v1::Device ipv4/ipv6/mac/bleAddress):
// one spelling each, whatever the vendor hands over. The setters normalise
// what they can parse and keep anything else as given, so the send refuses it
// with the value in the message instead of a silent blank.
#pragma once

#include <arpa/inet.h>

#include <cctype>
#include <string>

namespace phicore::adapter::v1::detail {

inline std::string trimmedAscii(const std::string &value)
{
    std::size_t begin = 0;
    std::size_t end = value.size();
    while (begin < end && std::isspace(static_cast<unsigned char>(value[begin])))
        ++begin;
    while (end > begin && std::isspace(static_cast<unsigned char>(value[end - 1])))
        --end;
    return value.substr(begin, end - begin);
}

/// `aa:bb:cc:dd:ee:ff` from colons, dashes, dots or none at all; anything
/// that is not twelve hex digits comes back trimmed and unchanged.
inline std::string normalizedMac(const std::string &value)
{
    const std::string text = trimmedAscii(value);
    std::string hex;
    for (char c : text) {
        if (c == ':' || c == '-' || c == '.')
            continue;
        if (!std::isxdigit(static_cast<unsigned char>(c)))
            return text;
        hex.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
    }
    if (hex.size() != 12)
        return text;
    std::string out;
    for (std::size_t i = 0; i < hex.size(); i += 2) {
        if (!out.empty())
            out.push_back(':');
        out.append(hex, i, 2);
    }
    return out;
}

inline bool isNormalizedMac(const std::string &value)
{
    return value.size() == 17 && normalizedMac(value) == value;
}

/// The canonical text form (inet_ntop) of an address inet_pton accepts for
/// `family`; anything else trimmed and unchanged.
inline std::string normalizedIp(const std::string &value, int family)
{
    const std::string text = trimmedAscii(value);
    unsigned char raw[16];
    if (inet_pton(family, text.c_str(), raw) != 1)
        return text;
    char out[INET6_ADDRSTRLEN];
    if (!inet_ntop(family, raw, out, sizeof(out)))
        return text;
    return out;
}

inline bool isIp(const std::string &value, int family)
{
    unsigned char raw[16];
    return inet_pton(family, value.c_str(), raw) == 1;
}

} // namespace phicore::adapter::v1::detail

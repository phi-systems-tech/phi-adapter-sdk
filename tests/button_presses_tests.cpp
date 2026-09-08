// Either a single click or a double click, never both, decided half a second
// after the release.

#include <phi/adapter/testing/check.h>

#include "phi/adapter/sdk/button_presses.h"

#include <string>
#include <vector>

using phicore::adapter::sdk::ButtonPresses;
using Code = phicore::adapter::v1::ButtonEventCode;

namespace {

std::vector<Code> codesOf(const std::vector<ButtonPresses::Report> &reports)
{
    std::vector<Code> codes;
    for (const ButtonPresses::Report &report : reports)
        codes.push_back(report.code);
    return codes;
}

void testAClickIsSingleOrDoubleNeverBoth()
{
    ButtonPresses presses;
    const std::string key = "0x1:button1";

    // Press: said at once. Release: held, and a window opens.
    ButtonPresses::Outcome out = presses.onEvent(key, Code::InitialPress, 1000, 1000);
    PHI_CHECK(codesOf(out.report) == std::vector<Code>{Code::InitialPress});
    PHI_CHECK(!out.windowUntilMs.has_value());
    out = presses.onEvent(key, Code::ShortPressRelease, 1100, 1100);
    PHI_CHECK_MSG(out.report.empty(), "a release was reported before the window closed");
    PHI_CHECK(out.windowUntilMs.has_value()
              && *out.windowUntilMs == 1100 + ButtonPresses::kMultiPressWindowMs);
    std::vector<ButtonPresses::Report> closed = presses.onWindowClosed(key);
    PHI_CHECK(codesOf(closed) == std::vector<Code>{Code::ShortPressRelease});
    PHI_CHECK(closed.size() == 1 && closed[0].tsMs == 1100);
    PHI_CHECK(presses.onWindowClosed(key).empty());

    // Two releases inside the window: one double, no single.
    presses.onEvent(key, Code::InitialPress, 2000, 2000);
    out = presses.onEvent(key, Code::ShortPressRelease, 2100, 2100);
    PHI_CHECK(out.report.empty() && *out.windowUntilMs == 2600);
    presses.onEvent(key, Code::InitialPress, 2300, 2300);
    out = presses.onEvent(key, Code::ShortPressRelease, 2400, 2400);
    PHI_CHECK(out.report.empty());
    PHI_CHECK_MSG(*out.windowUntilMs == 2900, "the window did not move with the second click");
    closed = presses.onWindowClosed(key);
    PHI_CHECK_MSG(codesOf(closed) == std::vector<Code>{Code::DoublePress},
                  "a double click was not exactly one double press");
    PHI_CHECK(closed[0].tsMs == 2400);

    for (const std::int64_t t : {3000, 3200, 3400}) {
        presses.onEvent(key, Code::InitialPress, t, t);
        presses.onEvent(key, Code::ShortPressRelease, t + 50, t + 50);
    }
    PHI_CHECK(codesOf(presses.onWindowClosed(key)) == std::vector<Code>{Code::TriplePress});

    // A hold after a click: the click is a click, then the hold begins.
    presses.onEvent(key, Code::ShortPressRelease, 5000, 5000);
    out = presses.onEvent(key, Code::LongPress, 5200, 5200);
    PHI_CHECK(codesOf(out.report) == (std::vector<Code>{Code::ShortPressRelease, Code::LongPress}));
    PHI_CHECK(out.cancelWindow);
    out = presses.onEvent(key, Code::LongPress, 5600, 5600);
    PHI_CHECK(codesOf(out.report) == std::vector<Code>{Code::Repeat});
    out = presses.onEvent(key, Code::LongPressRelease, 5800, 5800);
    PHI_CHECK(codesOf(out.report) == std::vector<Code>{Code::LongPressRelease});
    out = presses.onEvent(key, Code::LongPress, 6000, 6000);
    PHI_CHECK(codesOf(out.report) == std::vector<Code>{Code::LongPress});

    // Hue says "repeat" without ever having said "long press": the hold is
    // announced once, then the repeats.
    presses.onEvent(key, Code::LongPressRelease, 6100, 6100);
    presses.onEvent(key, Code::InitialPress, 7000, 7000);
    out = presses.onEvent(key, Code::Repeat, 7800, 7800);
    PHI_CHECK(codesOf(out.report) == (std::vector<Code>{Code::LongPress, Code::Repeat}));
    out = presses.onEvent(key, Code::Repeat, 8600, 8600);
    PHI_CHECK(codesOf(out.report) == std::vector<Code>{Code::Repeat});
    out = presses.onEvent(key, Code::LongPressRelease, 8700, 8700);
    PHI_CHECK(codesOf(out.report) == std::vector<Code>{Code::LongPressRelease});

    // Another button is another story.
    out = presses.onEvent("0x1:button2", Code::ShortPressRelease, 9100, 9100);
    PHI_CHECK(out.report.empty() && out.windowUntilMs.has_value());

    // A device that counts for itself is believed at once.
    out = presses.onEvent(key, Code::DoublePress, 9500, 9500);
    PHI_CHECK(codesOf(out.report) == std::vector<Code>{Code::DoublePress});
    PHI_CHECK(!out.windowUntilMs.has_value());

    // The window runs on arrival, the report on the device's time. A release
    // stamped 10000 that arrives at 10300 keeps the window open until 10800,
    // and the click, once reported, is dated 10000.
    out = presses.onEvent(key, Code::ShortPressRelease, 10000, 10300);
    PHI_CHECK_MSG(*out.windowUntilMs == 10800, "the window ran on the device's clock");
    out = presses.onEvent(key, Code::ShortPressRelease, 10400, 10700);
    PHI_CHECK(*out.windowUntilMs == 11200);
    const std::vector<ButtonPresses::Report> late = presses.onWindowClosed(key);
    PHI_CHECK(codesOf(late) == std::vector<Code>{Code::DoublePress});
    PHI_CHECK(late[0].tsMs == 10400);
}

} // namespace

int main()
{
    testAClickIsSingleOrDoubleNeverBoth();
    return phi::testing::report("sdk_button_presses_tests");
}

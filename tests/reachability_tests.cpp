// A device that goes quiet: waited out while it may still be a blip, said
// once when it is real, and asked for less often for as long as it lasts.

#include <phi/adapter/testing/check.h>

#include "phi/adapter/sdk/reachability.h"

#include <string>

using phicore::adapter::sdk::Reachability;
using State = Reachability::State;

namespace {

Reachability::Policy policy()
{
    Reachability::Policy out;
    out.intervalMs = 5000;
    out.strikes = 3;
    out.retryDelaysMs = {10000, 20000, 60000};
    return out;
}

void testABlipIsWaitedOutAndNotAnnounced()
{
    Reachability thing(policy());

    Reachability::Verdict verdict = thing.missed("no answer", 1000);
    PHI_CHECK(!verdict.say && !verdict.changed && verdict.state == State::Up);
    PHI_CHECK_MSG(verdict.waitMs == 10000, "waitMs: %lld", static_cast<long long>(verdict.waitMs));
    PHI_CHECK(thing.misses() == 1 && !thing.down());

    verdict = thing.missed("no answer", 11000);
    PHI_CHECK(!verdict.say && verdict.waitMs == 20000 && verdict.state == State::Up);

    // It came back before the third: nothing was ever said about it, and the
    // wait is the healthy one again.
    verdict = thing.answered(31000);
    PHI_CHECK(!verdict.say && !verdict.changed && verdict.waitMs == 5000);
    PHI_CHECK(thing.misses() == 0 && thing.reason().empty());
}

void testTheThirdMissIsSaidOnceAndThenNotAgain()
{
    Reachability thing(policy());
    thing.missed("no answer", 1000);
    thing.missed("no answer", 11000);

    Reachability::Verdict verdict = thing.missed("no answer", 31000);
    PHI_CHECK(verdict.changed && verdict.say && verdict.state == State::Down);
    PHI_CHECK(thing.down() && thing.reason() == "no answer");

    // Every miss after that is the same news, and the wait has run to the end
    // of the list, where it stays.
    for (std::int64_t at = 91000; at < 600000; at += 60000) {
        verdict = thing.missed("no answer", at);
        PHI_CHECK_MSG(!verdict.say && !verdict.changed, "said it again at %lld", static_cast<long long>(at));
        PHI_CHECK(verdict.waitMs == 60000 && verdict.state == State::Down);
    }

    // Back: said once, and the counting starts over.
    verdict = thing.answered(700000);
    PHI_CHECK(verdict.changed && verdict.say && verdict.state == State::Up && verdict.waitMs == 5000);
    verdict = thing.answered(705000);
    PHI_CHECK(!verdict.changed && !verdict.say);
}

void testANewReasonIsWorthSayingOnceItIsGone()
{
    Reachability thing(policy());
    thing.missed("connection refused", 0);
    thing.missed("connection refused", 1000);
    PHI_CHECK(thing.missed("connection refused", 2000).say);

    Reachability::Verdict verdict = thing.missed("no route to host", 3000);
    PHI_CHECK_MSG(verdict.say && !verdict.changed, "a reason nobody has heard was swallowed");
    PHI_CHECK(!thing.missed("no route to host", 4000).say);
}

void testSayingItAgainAfterAWhile()
{
    Reachability::Policy repeating = policy();
    repeating.sayAgainAfterMs = 300000;
    Reachability thing(repeating);
    thing.missed("no answer", 0);
    thing.missed("no answer", 1000);
    PHI_CHECK(thing.missed("no answer", 2000).say);

    PHI_CHECK(!thing.missed("no answer", 200000).say);
    PHI_CHECK(thing.missed("no answer", 302000).say);
    PHI_CHECK(!thing.missed("no answer", 400000).say);
    PHI_CHECK(thing.missed("no answer", 602001).say);
}

void testTheShapesWithoutABacklog()
{
    // One strike, no delay list: gone at the first miss, waited out at the
    // one interval the caller has - the shape an adapter with a single retry
    // interval had before.
    Reachability::Policy flat;
    flat.intervalMs = 5000;
    flat.strikes = 1;
    flat.retryDelaysMs.clear();
    Reachability thing(flat);
    Reachability::Verdict verdict = thing.missed("gone", 0);
    PHI_CHECK(verdict.changed && verdict.say && verdict.waitMs == 5000 && verdict.state == State::Down);

    // A second transport took over; the poll behind it is a safety net.
    thing.setIntervalMs(60000);
    PHI_CHECK(thing.answered(1000).waitMs == 60000);

    // Forgetting is silent: a new session says nothing about the old one.
    thing.missed("gone", 2000);
    PHI_CHECK(thing.down());
    thing.forget();
    PHI_CHECK(!thing.down() && thing.misses() == 0);
    PHI_CHECK(!thing.answered(3000).say);
}

void testAPolicyThatMakesNoSense()
{
    Reachability::Policy odd;
    odd.strikes = 0;
    odd.intervalMs = -1;
    odd.retryDelaysMs = {-5};
    Reachability thing(odd);
    const Reachability::Verdict verdict = thing.missed("gone", 0);
    PHI_CHECK(verdict.changed && verdict.waitMs == 0);
}

} // namespace

int main()
{
    testABlipIsWaitedOutAndNotAnnounced();
    testTheThirdMissIsSaidOnceAndThenNotAgain();
    testANewReasonIsWorthSayingOnceItIsGone();
    testSayingItAgainAfterAWhile();
    testTheShapesWithoutABacklog();
    testAPolicyThatMakesNoSense();
    return phi::testing::report("sdk_reachability_tests");
}

// The version an adapter was built against is read where the adapter compiles
// and handed to the library by the inline constructors; the library has a
// number of its own. Here both are the same build, so they agree - what is
// checked is that each path carries the value it is meant to.

#include <phi/adapter/testing/check.h>

#include "phi/adapter/sdk/sidecar.h"
#include "test_support.h"

#include <cstring>
#include <string>

namespace sdk = phicore::adapter::sdk;

namespace {

void testMacrosAgreeWithEachOther()
{
    const std::string spelled = std::to_string(PHI_ADAPTER_SDK_VERSION_MAJOR) + "."
        + std::to_string(PHI_ADAPTER_SDK_VERSION_MINOR) + "." + std::to_string(PHI_ADAPTER_SDK_VERSION_PATCH);
    PHI_CHECK_MSG(spelled == PHI_ADAPTER_SDK_VERSION, "%s vs %s", spelled.c_str(), PHI_ADAPTER_SDK_VERSION);
    PHI_CHECK(PHI_ADAPTER_SDK_VERSION_NUMBER
              == PHI_ADAPTER_SDK_VERSION_CHECK(PHI_ADAPTER_SDK_VERSION_MAJOR, PHI_ADAPTER_SDK_VERSION_MINOR,
                                               PHI_ADAPTER_SDK_VERSION_PATCH));
    PHI_CHECK(PHI_ADAPTER_SDK_VERSION_NUMBER >= PHI_ADAPTER_SDK_VERSION_CHECK(0, 26, 0));
    PHI_CHECK(std::strcmp(phicore::adapter::v1::kSdkVersion, PHI_ADAPTER_SDK_VERSION) == 0);
}

void testTheLibraryHasANumberOfItsOwn()
{
    // Same build here; on a box with an updated library and an old adapter the
    // two differ, and that difference is the point of the boundary.
    PHI_CHECK(std::strcmp(sdk::sdkLibraryVersion(), PHI_ADAPTER_SDK_VERSION) == 0);
}

void testTheDefaultConstructorCapturesTheCallersVersion()
{
    sdk::SidecarDispatcher dispatcher(phitest::uniqueSocketPath("version-default"));
    PHI_CHECK(std::strcmp(dispatcher.builtAgainstSdkVersion(), PHI_ADAPTER_SDK_VERSION) == 0);
}

void testAPinnedVersionIsKeptAsGiven()
{
    sdk::SidecarDispatcher dispatcher(phitest::uniqueSocketPath("version-pinned"), "0.0.0-golden");
    PHI_CHECK(std::strcmp(dispatcher.builtAgainstSdkVersion(), "0.0.0-golden") == 0);

    sdk::SidecarDispatcher blank(phitest::uniqueSocketPath("version-blank"), nullptr);
    PHI_CHECK(std::strcmp(blank.builtAgainstSdkVersion(), "") == 0);
}

} // namespace

int main()
{
    testMacrosAgreeWithEachOther();
    testTheLibraryHasANumberOfItsOwn();
    testTheDefaultConstructorCapturesTheCallersVersion();
    testAPinnedVersionIsKeptAsGiven();
    return phi::testing::report("sdk_version_tests");
}

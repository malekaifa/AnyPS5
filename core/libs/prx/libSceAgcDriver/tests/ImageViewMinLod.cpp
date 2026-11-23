#include "prx/libSceAgcDriver/Execution/include/ImageViewMinLod.hpp"
#include <cstdio>

namespace {

struct Case {
    VkDriverId driver;
    bool reported;
    bool usable;
};

constexpr Case Cases[]{
    {VK_DRIVER_ID_INTEL_OPEN_SOURCE_MESA, true, false},
    {VK_DRIVER_ID_INTEL_OPEN_SOURCE_MESA, false, false},
    {VK_DRIVER_ID_INTEL_PROPRIETARY_WINDOWS, true, true},
    {VK_DRIVER_ID_MESA_RADV, true, true},
    {VK_DRIVER_ID_NVIDIA_PROPRIETARY, true, true},
    {VK_DRIVER_ID_MOLTENVK, true, true},
    {VK_DRIVER_ID_MESA_LLVMPIPE, true, true},
    {VK_DRIVER_ID_NVIDIA_PROPRIETARY, false, false},
};

}

int main() {
    int failures = 0;
    for (const auto& item : Cases) {
        if (AgcDriver::ImageViewMinLodUsable(item.driver, item.reported) != item.usable) {
            std::fprintf(stderr, "driver %d reported %d: expected image-view minimum LOD %s\n", static_cast<int>(item.driver), item.reported ? 1 : 0, item.usable ? "usable" : "unusable");
            ++failures;
        }
    }
    return failures == 0 ? 0 : 1;
}

#ifndef CORE_LIBS_PRX_LIBSCEAGCDRIVER_EXECUTION_INCLUDE_IMAGEVIEWMINLOD_HPP
#define CORE_LIBS_PRX_LIBSCEAGCDRIVER_EXECUTION_INCLUDE_IMAGEVIEWMINLOD_HPP

#include <vulkan/vulkan.h>

namespace AgcDriver {

inline bool ImageViewMinLodUsable(VkDriverId driver, bool reported) {
    return reported && driver != VK_DRIVER_ID_INTEL_OPEN_SOURCE_MESA;
}

}

#endif

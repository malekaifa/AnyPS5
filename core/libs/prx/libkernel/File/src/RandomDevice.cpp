#include "prx/libkernel/File/include/RandomDevice.hpp"
#include "prx/libc/include/General.hpp"
#include "prx/libc/include/HostEntropy.hpp"
#include "prx/libkernel/File/include/DescriptorControl.hpp"
#include "prx/libkernel/File/include/FileFlags.hpp"

#include <algorithm>
#include <array>
#include <cerrno>
#include <limits>
#include <mutex>
#include <set>

#ifdef _WIN32
#include <fcntl.h>
#include <io.h>
#else
#include <fcntl.h>
#include <unistd.h>
#endif

namespace {

std::mutex g_mutex;
std::set<int> g_descriptors;

}

namespace File {

bool IsRandomDevicePath(std::string_view path) {
    return path == "/dev/random" || path == "/dev/urandom";
}

int OpenRandomDevice(int access) {
#ifdef _WIN32
    const int nativeAccess = access == SCE_KERNEL_O_WRONLY ? _O_WRONLY : access == SCE_KERNEL_O_RDWR ? _O_RDWR : _O_RDONLY;
    const int fd = ::_open("NUL", nativeAccess | _O_BINARY);
#else
    const int nativeAccess = access == SCE_KERNEL_O_WRONLY ? O_WRONLY : access == SCE_KERNEL_O_RDWR ? O_RDWR : O_RDONLY;
    const int fd = ::open("/dev/null", nativeAccess);
#endif
    if (fd < 0) return -1;
    std::lock_guard lock(g_mutex);
    g_descriptors.insert(fd);
    return fd;
}

bool IsRandomDevice(int fd) {
    std::lock_guard lock(g_mutex);
    return g_descriptors.contains(fd);
}

bool ReadRandomDevice(int fd, void* buf, std::size_t nbytes) {
    std::lock_guard lock(g_mutex);
    if (!g_descriptors.contains(fd)) return false;
    HostEntropy_nid_no_patch(buf, nbytes);
    return true;
}

std::int64_t WriteRandomDevice(int fd, const void* buf, std::size_t nbytes) {
    if (!IsRandomDevice(fd) || !GrantsAccess(fd, true)) {
        errno = EBADF;
        return -1;
    }
    if (nbytes > static_cast<std::size_t>(std::numeric_limits<std::int64_t>::max())) {
        errno = EINVAL;
        return -1;
    }
    const auto start = reinterpret_cast<std::uintptr_t>(buf);
    if (nbytes != 0 && (start == 0 || nbytes > UINTPTR_MAX - start)) {
        errno = EFAULT;
        return -1;
    }
    std::array<unsigned char, 4096> copy{};
    for (std::size_t offset = 0; offset < nbytes;) {
        const std::size_t count = std::min(copy.size(), nbytes - offset);
        const auto* source = reinterpret_cast<const void*>(start + offset);
        if (!GuestCopyIn_nid_no_patch(copy.data(), source, count)) {
            errno = EFAULT;
            return -1;
        }
        offset += count;
    }
    return static_cast<std::int64_t>(nbytes);
}

void RememberRandomDevice(int fd) {
    std::lock_guard lock(g_mutex);
    g_descriptors.insert(fd);
}

void ForgetRandomDevice(int fd) {
    std::lock_guard lock(g_mutex);
    g_descriptors.erase(fd);
}

}

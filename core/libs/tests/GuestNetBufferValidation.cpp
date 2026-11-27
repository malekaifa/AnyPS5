#include "prx/libkernel/include/Api.hpp"
#include "prx/libc/include/Api.hpp"
#include "prx/libSceNet/include/Net.hpp"
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <winsock2.h>
#include <windows.h>
#else
#include <sys/socket.h>
#include <sys/mman.h>
#include <unistd.h>
#endif
#include <climits>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <source_location>
#include "prx/libc/include/general/VabiMacros.hpp"
#include "prx/libkernel/Socket/include/SocketPoll.hpp"

static void Require(bool condition, std::source_location location = std::source_location::current()) {
    if (!condition) {
        std::fprintf(stderr, "check failed at line %u\n", location.line());
        std::abort();
    }
}

class NoAccessPage {
    void* address = nullptr;
    std::size_t size = 0;

public:
    NoAccessPage() {
#ifdef _WIN32
        SYSTEM_INFO info{};
        GetSystemInfo(&info);
        size = info.dwPageSize;
        address = VirtualAlloc(nullptr, size, MEM_RESERVE | MEM_COMMIT, PAGE_NOACCESS);
#else
        const long pageSize = sysconf(_SC_PAGESIZE);
        Require(pageSize > 0);
        size = static_cast<std::size_t>(pageSize);
        address = mmap(nullptr, size, PROT_NONE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
        if (address == MAP_FAILED) address = nullptr;
#endif
        Require(address != nullptr);
    }

    ~NoAccessPage() {
#ifdef _WIN32
        Require(VirtualFree(address, 0, MEM_RELEASE) != 0);
#else
        Require(munmap(address, size) == 0);
#endif
    }

    const void* Get() const { return address; }
};

static void CheckTimeoutGuestPointerFaults(int socket) {
    constexpr int SocketLevel = 0xffff;
    constexpr int SendTimeout = 0x1005;
    constexpr int ReceiveTimeout = 0x1006;
    constexpr int Fault = 14;
    constexpr auto TimevalSize = static_cast<std::uint32_t>(2 * sizeof(std::int64_t));
    const auto check = [&](const void* value) {
        for (const int option : {SendTimeout, ReceiveTimeout}) {
            Require(sceNetSetsockopt(socket, SocketLevel, option, value, TimevalSize) ==
                static_cast<int>(0x80410100u | static_cast<unsigned>(Fault)));
            Require(*sceNetErrnoLoc() == Fault);
            Require(setsockopt_nid_postfix(socket, SocketLevel, option, value, TimevalSize) == -1);
            Require(*__error_nid_postfix() == Fault);
        }
    };

    check(reinterpret_cast<const void*>(std::uintptr_t{0x10}));
    NoAccessPage noAccess;
    check(noAccess.Get());
}

int main() {
    Require(sceNetInit_nid_postfix() == 0);
    for (const int family : {2, 28}) {
        for (const int type : {1, 2}) {
            const int socket = sceNetSocket("buffer-validation", family, type, type == 1 ? 6 : 17);
            Require(socket >= 0);
            std::shared_ptr<void> owner;
            std::uintptr_t native = 0;
            Require(KernelFindSocketNative_nid_no_patch(socket, &owner, &native));
            CheckTimeoutGuestPointerFaults(socket);
            for (const int option : {0x1001, 0x1002}) {
                const auto nativeBuffer = [&] {
                    int value = 0;
#ifdef _WIN32
                    int size = sizeof(value);
#else
                    socklen_t size = sizeof(value);
#endif
                    Require(::getsockopt(native, SOL_SOCKET, option == 0x1001 ? SO_SNDBUF : SO_RCVBUF,
                        reinterpret_cast<char*>(&value), &size) == 0);
                    return value;
                };
                const int initial = 32768;
                Require(sceNetSetsockopt(socket, 0xffff, option, &initial, sizeof(initial)) == 0);
                const int originalNative = nativeBuffer();
                for (const int invalid : {0, -1, -4096, INT_MIN}) {
                    Require(sceNetSetsockopt(socket, 0xffff, option, &invalid, sizeof(invalid)) == static_cast<int>(0x80410116u));
                    Require(*sceNetErrnoLoc() == 22);
                    Require(setsockopt_nid_postfix(socket, 0xffff, option, &invalid, sizeof(invalid)) == -1);
                    Require(*__error_nid_postfix() == 22);
                    int value = 0;
                    std::uint32_t size = sizeof(value);
                    Require(sceNetGetsockopt(socket, 0xffff, option, &value, &size) == 0 && value == initial);
                    Require(nativeBuffer() == originalNative);
                }
                Require(sceNetSetsockopt(socket, 0xffff, option, nullptr, sizeof(initial)) == static_cast<int>(0x80410116u));
                Require(sceNetSetsockopt(socket, 0xffff, option, &initial, 3) == static_cast<int>(0x80410116u));
                const int valid = 8192;
                Require(sceNetSetsockopt(socket, 0xffff, option, &valid, sizeof(valid)) == 0);
                int value = 0;
                std::uint32_t size = sizeof(value);
                Require(sceNetGetsockopt(socket, 0xffff, option, &value, &size) == 0 && value == valid);
            }
            Require(sceNetSocketClose(socket) == 0);
        }
    }
    const int zero = 0;
    Require(sceNetSetsockopt(-1, 0xffff, 0x1001, &zero, sizeof(zero)) == static_cast<int>(0x80410109u));
    Require(*sceNetErrnoLoc() == 9);
}

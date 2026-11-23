#include "prx/libkernel/include/Api.hpp"
#include "SceTypes.hpp"
#include "prx/libc/include/GuestAllocations.hpp"
#include <cerrno>
#include <source_location>
#include <cstdio>
#ifdef __linux__
#include <atomic>
#include <chrono>
#include <thread>
#include <string>
#include <vector>
#include <sys/mman.h>
#include <sys/syscall.h>
#include <unistd.h>
#endif
#ifdef _WIN32
#include "prx/libc/include/GuestArena.hpp"
#include "prx/libkernel/Pthread/include/ThreadLifecycle.hpp"
#include "prx/libkernel/Time/include/Time.hpp"
#include <atomic>
#include <chrono>
#include <thread>
#include <vector>
#include <windows.h>
#include <process.h>
#endif
#include <cstddef>
#include <cstdint>
#include <cstdlib>

extern "C" {

#ifdef __linux__

#endif
}

static constexpr int SCE_OK = 0;
static constexpr std::size_t FRAME_SIZE = 4096;
static constexpr std::size_t FRAME_MARGIN = 2 * FRAME_SIZE;
#if defined(__APPLE__) || defined(ANYPS5_ENABLE_ASAN)
static constexpr std::size_t LOCAL_SLACK = 2 * FRAME_SIZE;
#else
static constexpr std::size_t LOCAL_SLACK = FRAME_SIZE;
#endif
static constexpr std::size_t HOST_CALL_STACK = 256 * 1024;
static constexpr std::size_t STACK_SIZES[] = {16384, 65536, 1u << 20, 16u << 20};
#ifdef __linux__
static void* lastGuestStack = nullptr;
static std::size_t lastGuestStackSize = 0;
#endif

#ifdef __linux__
extern bool GuestThreadStackHostRangeOverlaps(std::uintptr_t start, std::uintptr_t end);
static std::atomic<unsigned> observedUnmaps{0};

static void ObserveUnmap(std::uintptr_t address, std::size_t bytes) {
    if (GuestThreadStackHostRangeOverlaps(address, address + bytes)) std::abort();
    observedUnmaps.fetch_add(1, std::memory_order_relaxed);
}
#endif

thread_local unsigned char threadLocalBlock[512 * 1024];

static void Require(bool value, std::source_location location = std::source_location::current()) {
    if (value) return;
    std::fprintf(stderr, "GuestPthreadStack failed at line %u\n", location.line());
    std::abort();
}

static std::uintptr_t Descend(std::uintptr_t floor);
static std::uintptr_t (*volatile descend)(std::uintptr_t) = Descend;

static std::uintptr_t Descend(std::uintptr_t floor) {
    volatile unsigned char frame[FRAME_SIZE];
    frame[0] = 0x5a;
    frame[FRAME_SIZE - 1] = 0xa5;
    const auto here = reinterpret_cast<std::uintptr_t>(&frame[0]);
    const auto deepest = here < floor + FRAME_MARGIN ? here : descend(floor);
    Require(frame[0] == 0x5a && frame[FRAME_SIZE - 1] == 0xa5);
    return deepest;
}

static void* APS5_VABI Worker(void* arg) {
    const auto requested = *static_cast<const std::size_t*>(arg);
    threadLocalBlock[0] = 1;
    threadLocalBlock[sizeof(threadLocalBlock) - 1] = 2;

    PthreadAttr attr = nullptr;
    Require(scePthreadAttrInit(&attr) == SCE_OK);
    Require(scePthreadAttrGet(scePthreadSelf(), &attr) == SCE_OK);
    void* address = nullptr;
    std::size_t size = 0;
    Require(scePthreadAttrGetstack(&attr, &address, &size) == SCE_OK);
#ifdef __linux__
    lastGuestStack = address;
    lastGuestStackSize = size;
#endif
    Require(scePthreadAttrDestroy(&attr) == SCE_OK);
    Require(size == requested);
    Require(reinterpret_cast<std::uintptr_t>(address) >= 0x100000000ull);

    const auto begin = reinterpret_cast<std::uintptr_t>(address);
    const auto local = reinterpret_cast<std::uintptr_t>(&size);
    Require(local > begin && local < begin + size);
    Require(local - begin + LOCAL_SLACK >= requested);
    const auto deepest = Descend(begin);
    Require(deepest >= begin && deepest < begin + FRAME_MARGIN);
    const auto hostDeepest = descend(begin - HOST_CALL_STACK);
    Require(hostDeepest < begin - HOST_CALL_STACK + FRAME_MARGIN);

    Require(threadLocalBlock[0] == 1 && threadLocalBlock[sizeof(threadLocalBlock) - 1] == 2);
    return arg;
}

#ifdef __linux__
struct StackProbe {
    std::size_t requested = 0;
    std::uintptr_t stack = 0;
    std::uintptr_t markerAddress = 0;
    std::uintptr_t marker = 0;
    std::atomic<bool> done{false};
    int hostTid = 0;
};

static void* APS5_VABI RetainedStackWorker(void* opaque) {
    auto& probe = *static_cast<StackProbe*>(opaque);
    PthreadAttr attr = nullptr;
    Require(scePthreadAttrInit(&attr) == SCE_OK);
    Require(scePthreadAttrGet(scePthreadSelf(), &attr) == SCE_OK);
    void* address = nullptr;
    std::size_t size = 0;
    Require(scePthreadAttrGetstack(&attr, &address, &size) == SCE_OK);
    Require(scePthreadAttrDestroy(&attr) == SCE_OK);
    Require(size == probe.requested);
    probe.stack = reinterpret_cast<std::uintptr_t>(address);
    probe.markerAddress = probe.stack + size - 4096;
    *reinterpret_cast<std::uintptr_t*>(probe.markerAddress) = probe.marker;
    Require(*reinterpret_cast<const std::uintptr_t*>(probe.markerAddress) == probe.marker);
    probe.hostTid = static_cast<int>(syscall(SYS_gettid));
    probe.done.store(true, std::memory_order_release);
    return nullptr;
}

static void WaitForProbe(StackProbe& probe) {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
    while (!probe.done.load(std::memory_order_acquire)) {
        Require(std::chrono::steady_clock::now() < deadline);
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    const auto task = "/proc/self/task/" + std::to_string(probe.hostTid);
    while (access(task.c_str(), F_OK) == 0) {
        Require(std::chrono::steady_clock::now() < deadline);
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
}

static void RequireCachedStackDiscarded(const StackProbe& probe) {
    const auto range = GuestAllocations::GuestAllocationsLookup_nid_postfix(probe.markerAddress, false);
    Require(range != nullptr && range->readable && range->writable);
    Require(*reinterpret_cast<const std::uintptr_t*>(probe.markerAddress) == 0);
}

static bool ReusesOneOf(const StackProbe& probe, const StackProbe* previous, std::size_t count) {
    for (std::size_t i = 0; i < count; ++i) if (probe.stack == previous[i].stack) return true;
    return false;
}
#endif

#ifdef _WIN32
static constexpr std::size_t EXIT_CANCEL_STACK = 65536;
static constexpr int EXIT_CANCEL_COUNT = 4;
static void* const CANCELED_VALUE = reinterpret_cast<void*>(std::uintptr_t{1});
static std::atomic<int> cancelReady{0};

static void* APS5_VABI ExitWorker(void* arg) {
    scePthreadExit(arg);
    return nullptr;
}

static void* APS5_VABI CancelWorker(void*) {
    cancelReady.fetch_add(1, std::memory_order_release);
    for (;;) sceKernelUsleep_nid_postfix(1000);
}

static unsigned __stdcall AdoptedExit(void*) {
    scePthreadSelf();
    scePthreadExit(nullptr);
    return 1;
}

static void RequireUncommitted(std::uintptr_t address) {
    MEMORY_BASIC_INFORMATION memory{};
    Require(VirtualQuery(reinterpret_cast<void*>(address), &memory, sizeof(memory)) == sizeof(memory));
    Require(memory.State != MEM_COMMIT);
}

static std::uintptr_t QueryGuestLow(Pthread thread) {
    PthreadAttr attr = nullptr;
    Require(scePthreadAttrInit(&attr) == SCE_OK);
    Require(scePthreadAttrGet(thread, &attr) == SCE_OK);
    void* address = nullptr;
    std::size_t size = 0;
    Require(scePthreadAttrGetstack(&attr, &address, &size) == SCE_OK);
    Require(scePthreadAttrDestroy(&attr) == SCE_OK);
    Require(size == EXIT_CANCEL_STACK);
    Require(reinterpret_cast<std::uintptr_t>(address) >= 0x100000000ull);
    return reinterpret_cast<std::uintptr_t>(address);
}
#endif

int main() {
#ifdef __linux__
    GuestAllocations::GuestAllocationsSetUnmapObserver_nid_postfix(&ObserveUnmap);
#endif
    for (auto stackSize : STACK_SIZES) {
        PthreadAttr attr = nullptr;
        Require(scePthreadAttrInit(&attr) == SCE_OK);
        Require(scePthreadAttrSetstacksize(&attr, stackSize) == SCE_OK);
        Pthread thread = nullptr;
        Require(scePthreadCreate(&thread, &attr, Worker, &stackSize, nullptr) == SCE_OK);
        Require(scePthreadAttrDestroy(&attr) == SCE_OK);
        void* result = nullptr;
        Require(scePthreadJoin(thread, &result) == SCE_OK);
        Require(result == &stackSize);
#ifdef __linux__
        if (stackSize == STACK_SIZES[sizeof(STACK_SIZES) / sizeof(STACK_SIZES[0]) - 1]) {
            const auto pageSize = static_cast<std::size_t>(sysconf(_SC_PAGESIZE));
            const auto stackAddress = reinterpret_cast<std::uintptr_t>(lastGuestStack);
            const auto firstPageAddress = stackAddress - stackAddress % pageSize;
            const auto leadingBytes = stackAddress - firstPageAddress;
            const auto pageCount = (leadingBytes + lastGuestStackSize + pageSize - 1) / pageSize;
            std::vector<unsigned char> residency(pageCount);
            if (mincore(reinterpret_cast<void*>(firstPageAddress), pageCount * pageSize, residency.data()) == 0) {
                for (std::size_t page = 0; page < pageCount; ++page)
                    Require((residency[page] & 1u) == 0);
            } else {
                Require(errno == ENOMEM);
            }
        }
#endif
    }
#ifdef __linux__
    constexpr std::size_t JoinedStackSize = 81920;
    StackProbe joined[6];
    for (std::size_t i = 0; i < 6; ++i) {
        joined[i].requested = JoinedStackSize;
        joined[i].marker = 0x77000000u + i;
        PthreadAttr attr = nullptr;
        Require(scePthreadAttrInit(&attr) == SCE_OK);
        Require(scePthreadAttrSetstacksize(&attr, JoinedStackSize) == SCE_OK);
        Pthread thread = nullptr;
        Require(scePthreadCreate(&thread, &attr, RetainedStackWorker, &joined[i], nullptr) == SCE_OK);
        Require(scePthreadAttrDestroy(&attr) == SCE_OK);
        Require(scePthreadJoin(thread, nullptr) == SCE_OK);
        RequireCachedStackDiscarded(joined[i]);
    }
    StackProbe joinedReuse;
    joinedReuse.requested = JoinedStackSize;
    joinedReuse.marker = 0x7700ffffu;
    PthreadAttr joinedAttr = nullptr;
    Require(scePthreadAttrInit(&joinedAttr) == SCE_OK);
    Require(scePthreadAttrSetstacksize(&joinedAttr, JoinedStackSize) == SCE_OK);
    Pthread joinedThread = nullptr;
    Require(scePthreadCreate(&joinedThread, &joinedAttr, RetainedStackWorker, &joinedReuse, nullptr) == SCE_OK);
    Require(scePthreadAttrDestroy(&joinedAttr) == SCE_OK);
    Require(scePthreadJoin(joinedThread, nullptr) == SCE_OK);
    Require(ReusesOneOf(joinedReuse, joined, 6));
    RequireCachedStackDiscarded(joinedReuse);

    constexpr std::size_t DetachedStackSize = 114688;
    StackProbe detached[5];
    for (std::size_t i = 0; i < 5; ++i) {
        detached[i].requested = DetachedStackSize;
        detached[i].marker = 0x77300000u + i;
        PthreadAttr attr = nullptr;
        Require(scePthreadAttrInit(&attr) == SCE_OK);
        Require(scePthreadAttrSetstacksize(&attr, DetachedStackSize) == SCE_OK);
        Pthread thread = nullptr;
        Require(scePthreadCreate(&thread, &attr, RetainedStackWorker, &detached[i], nullptr) == SCE_OK);
        Require(scePthreadAttrDestroy(&attr) == SCE_OK);
        Require(scePthreadDetach(thread) == SCE_OK);
        WaitForProbe(detached[i]);
    }
    StackProbe detachedReuse;
    detachedReuse.requested = DetachedStackSize;
    detachedReuse.marker = 0x7730ffffu;
    PthreadAttr detachedAttr = nullptr;
    Require(scePthreadAttrInit(&detachedAttr) == SCE_OK);
    Require(scePthreadAttrSetstacksize(&detachedAttr, DetachedStackSize) == SCE_OK);
    Pthread detachedThread = nullptr;
    Require(scePthreadCreate(&detachedThread, &detachedAttr, RetainedStackWorker, &detachedReuse, nullptr) == SCE_OK);
    Require(scePthreadAttrDestroy(&detachedAttr) == SCE_OK);
    Require(scePthreadJoin(detachedThread, nullptr) == SCE_OK);
    for (const auto& probe : detached) RequireCachedStackDiscarded(probe);
    Require(ReusesOneOf(detachedReuse, detached, 5));
    RequireCachedStackDiscarded(detachedReuse);

    for (std::size_t stackSize = 4u << 20; stackSize <= 20u << 20; stackSize += 1u << 20) {
        PthreadAttr attr = nullptr;
        Require(scePthreadAttrInit(&attr) == SCE_OK);
        Require(scePthreadAttrSetstacksize(&attr, stackSize) == SCE_OK);
        Pthread thread = nullptr;
        Require(scePthreadCreate(&thread, &attr, Worker, &stackSize, nullptr) == SCE_OK);
        Require(scePthreadAttrDestroy(&attr) == SCE_OK);
        void* result = nullptr;
        Require(scePthreadJoin(thread, &result) == SCE_OK);
        Require(result == &stackSize);
    }
    Require(observedUnmaps.load(std::memory_order_relaxed) != 0);
    GuestAllocations::GuestAllocationsSetUnmapObserver_nid_postfix(nullptr);
#endif
#ifdef _WIN32
    {
        std::vector<Pthread> exits;
        std::vector<std::uintptr_t> lows;
        std::vector<void*> args;
        args.reserve(EXIT_CANCEL_COUNT);
        for (int index = 0; index < EXIT_CANCEL_COUNT; ++index) {
            args.push_back(reinterpret_cast<void*>(static_cast<std::uintptr_t>(0xE110 + index)));
        }
        for (int index = 0; index < EXIT_CANCEL_COUNT; ++index) {
            PthreadAttr attr = nullptr;
            Require(scePthreadAttrInit(&attr) == SCE_OK);
            Require(scePthreadAttrSetstacksize(&attr, EXIT_CANCEL_STACK) == SCE_OK);
            Pthread thread = nullptr;
            Require(scePthreadCreate(&thread, &attr, ExitWorker, args[index], nullptr) == SCE_OK);
            Require(scePthreadAttrDestroy(&attr) == SCE_OK);
            lows.push_back(QueryGuestLow(thread));
            exits.push_back(thread);
        }
        for (int index = 0; index < EXIT_CANCEL_COUNT; ++index) {
            void* result = nullptr;
            Require(scePthreadJoin(exits[index], &result) == SCE_OK);
            Require(result == args[index]);
        }
        for (auto low : lows) RequireUncommitted(low);
    }
    {
        cancelReady.store(0, std::memory_order_release);
        std::vector<Pthread> cancels;
        std::vector<std::uintptr_t> lows;
        for (int index = 0; index < EXIT_CANCEL_COUNT; ++index) {
            PthreadAttr attr = nullptr;
            Require(scePthreadAttrInit(&attr) == SCE_OK);
            Require(scePthreadAttrSetstacksize(&attr, EXIT_CANCEL_STACK) == SCE_OK);
            Pthread thread = nullptr;
            Require(scePthreadCreate(&thread, &attr, CancelWorker, nullptr, nullptr) == SCE_OK);
            Require(scePthreadAttrDestroy(&attr) == SCE_OK);
            lows.push_back(QueryGuestLow(thread));
            cancels.push_back(thread);
        }
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
        while (cancelReady.load(std::memory_order_acquire) != EXIT_CANCEL_COUNT) {
            Require(std::chrono::steady_clock::now() < deadline);
            std::this_thread::yield();
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
        for (auto thread : cancels) Require(scePthreadCancel(thread) == SCE_OK);
        for (auto thread : cancels) {
            void* result = nullptr;
            Require(scePthreadJoin(thread, &result) == SCE_OK);
            Require(result == CANCELED_VALUE);
        }
        for (auto low : lows) RequireUncommitted(low);
    }
    {
        const auto raw = _beginthreadex(nullptr, 0, AdoptedExit, nullptr, 0, nullptr);
        Require(raw != 0);
        HANDLE handle = reinterpret_cast<HANDLE>(raw);
        Require(WaitForSingleObject(handle, 20000) == WAIT_OBJECT_0);
        DWORD code = 0;
        Require(GetExitCodeThread(handle, &code) != 0 && code == 0);
        Require(CloseHandle(handle) != 0);
    }
    {
        constexpr std::size_t page = 0x4000;
        void* reservation = nullptr;
        Require(sceKernelReserveVirtualRange(&reservation, page * 3, 0, 0) == 0);
        auto* bytes = static_cast<unsigned char*>(reservation);
        Require(mmap_nid_postfix(bytes, page * 3, 3, 0x1012, -1, 0) == bytes);
        bytes[0] = 0x42;
        bytes[page * 2] = 0x84;
        Require(sceKernelMunmap(reinterpret_cast<std::uint64_t>(bytes + page), page) == 0);
        Require(bytes[0] == 0x42);
        Require(bytes[page * 2] == 0x84);
        Require(sceKernelMunmap(reinterpret_cast<std::uint64_t>(bytes), page) == 0);
        Require(bytes[page * 2] == 0x84);
        Require(sceKernelMunmap(reinterpret_cast<std::uint64_t>(bytes + page * 2), page) == 0);
    }
    {
        constexpr std::size_t page = 0x4000;
        void* reservation = VirtualAlloc(nullptr, page * 3, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
        Require(reservation != nullptr);
        Require(!GuestArena::GuestArenaContains_nid_postfix(reservation, page * 3));
        auto* bytes = static_cast<unsigned char*>(reservation);
        {
            GuestAllocations::Mutation mutation;
            mutation.Add(bytes, page * 3, true, true, false);
        }
        bytes[0] = 0x42;
        bytes[page * 2] = 0x84;
        Require(sceKernelMunmap(reinterpret_cast<std::uint64_t>(bytes + page), page) == 0);
        MEMORY_BASIC_INFORMATION info{};
        Require(VirtualQuery(bytes, &info, sizeof(info)) == sizeof(info) && info.State == MEM_COMMIT);
        Require(VirtualQuery(bytes + page, &info, sizeof(info)) == sizeof(info) && info.State == MEM_RESERVE);
        Require(VirtualQuery(bytes + page * 2, &info, sizeof(info)) == sizeof(info) && info.State == MEM_COMMIT);
        Require(bytes[0] == 0x42);
        Require(bytes[page * 2] == 0x84);
        Require(sceKernelMunmap(reinterpret_cast<std::uint64_t>(bytes), page) == 0);
        Require(VirtualQuery(bytes, &info, sizeof(info)) == sizeof(info) && info.State == MEM_RESERVE);
        Require(VirtualQuery(bytes + page * 2, &info, sizeof(info)) == sizeof(info) && info.State == MEM_COMMIT);
        Require(bytes[page * 2] == 0x84);
        Require(sceKernelMunmap(reinterpret_cast<std::uint64_t>(bytes + page * 2), page) == 0);
        Require(VirtualQuery(bytes, &info, sizeof(info)) == sizeof(info) && info.State == MEM_FREE);
    }
    {
        constexpr std::size_t page = 0x4000;
        constexpr std::size_t count = page * 4;
        auto* bytes = static_cast<unsigned char*>(GuestArena::GuestArenaAllocate_nid_postfix(count, page));
        Require(bytes != nullptr);
        GuestArena::GuestArenaCommit_nid_postfix(bytes, count, PAGE_READWRITE, count);
        {
            GuestAllocations::Mutation mutation;
            mutation.Add(bytes, count, true, true, false);
        }
        Require(mprotect_nid_postfix(bytes + page, page, 1) == 0);
        Require(sceKernelMunmap(reinterpret_cast<std::uint64_t>(bytes), count) == 0);
        {
            GuestAllocations::Mutation mutation;
            Require(!mutation.Overlaps(bytes, count));
        }
        MEMORY_BASIC_INFORMATION info{};
        Require(VirtualQuery(bytes, &info, sizeof(info)) == sizeof(info) && info.State == MEM_RESERVE);
    }
#endif
}

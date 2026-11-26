#include "prx/libc/include/Api.hpp"
#include "prx/libc/include/general/VabiMacros.hpp"
#include <cstdint>
#include <cstdio>
#include <stdexcept>

static int failures = 0;

static void Require(bool condition, const char* message) {
    if (!condition) {
        std::fprintf(stderr, "%s\n", message);
        ++failures;
    }
}

static bool SeedIs(const std::uint16_t* seed, std::uint16_t low, std::uint16_t middle, std::uint16_t high) {
    return seed[0] == low && seed[1] == middle && seed[2] == high;
}

int main() {
    srand48_nid_postfix(0x12345678);
    Require(drand48_nid_postfix() == 0xb85403f4p-32, "drand48 did not advance the seeded state");

    const std::uint16_t replacement[3] = {0x1111, 0x2222, 0x3333};
    std::uint16_t* installed = seed48_nid_postfix(replacement);
    Require(installed != replacement, "seed48 returned the caller's replacement buffer");
    Require(SeedIs(installed, 0x1111, 0x2222, 0x3333), "seed48 did not return the new seed");
    Require(drand48_nid_postfix() == 0x14f99d82p-32, "seed48 did not install the new seed");
    Require(SeedIs(installed, 0x1111, 0x2222, 0x3333), "seed48 returned the live state rather than a copy");

    std::uint16_t* const again = seed48_nid_postfix(installed);
    Require(again == installed && SeedIs(again, 0x1111, 0x2222, 0x3333), "seed48 did not reuse its buffer when given it as input");
    Require(drand48_nid_postfix() == 0x14f99d82p-32, "seed48 did not restart from its returned seed");

    const std::uint16_t parameters[7] = {1, 0, 0, 3, 0, 0, 5};
    lcong48_nid_postfix(parameters);
    const std::uint16_t resetSeed[3] = {0x4444, 0x5555, 0x6666};
    installed = seed48_nid_postfix(resetSeed);
    Require(SeedIs(installed, 0x4444, 0x5555, 0x6666), "seed48 did not return the new seed after lcong48");
    Require(drand48_nid_postfix() == 0x7b5ed753p-32, "seed48 did not restore the default multiplier and addend");

    // Unreadable guest input is a host-safety fallback; console behavior is unmeasured.
    const std::uint16_t stableSeed[3] = {0x7777, 0x8888, 0x9999};
    std::uint16_t* const stableResult = seed48_nid_postfix(stableSeed);
    srand48_nid_postfix(0x12345678);
    const double expectedFirst = drand48_nid_postfix();
    const double expectedSecond = drand48_nid_postfix();
    srand48_nid_postfix(0x12345678);
    Require(drand48_nid_postfix() == expectedFirst, "seed48 unreadable-input baseline changed");
    bool seedThrew = false;
    try {
        seed48_nid_postfix(reinterpret_cast<const std::uint16_t*>(0x10));
    } catch (const std::invalid_argument&) {
        seedThrew = true;
    }
    Require(seedThrew, "seed48 accepted unreadable input");
    Require(SeedIs(stableResult, 0x7777, 0x8888, 0x9999), "seed48 changed its saved result after unreadable input");
    Require(drand48_nid_postfix() == expectedSecond, "seed48 changed rand48 state after unreadable input");

    lcong48_nid_postfix(parameters);
    const double configuredFirst = drand48_nid_postfix();
    const double configuredSecond = drand48_nid_postfix();
    lcong48_nid_postfix(parameters);
    Require(drand48_nid_postfix() == configuredFirst, "lcong48 unreadable-input baseline changed");
    bool lcongThrew = false;
    try {
        lcong48_nid_postfix(reinterpret_cast<const std::uint16_t*>(0x10));
    } catch (const std::invalid_argument&) {
        lcongThrew = true;
    }
    Require(lcongThrew, "lcong48 accepted unreadable input");
    Require(drand48_nid_postfix() == configuredSecond, "lcong48 changed rand48 state after unreadable input");

    if (failures != 0) return 1;
    std::puts("seed48 new-seed contract passed");
    return 0;
}

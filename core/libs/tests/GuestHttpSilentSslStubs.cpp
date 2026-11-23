#include "prx/libSceHttp/include/Http.hpp"
#include "prx/libc/include/general/VabiMacros.hpp"
#include <cstdint>
#include <cstdio>
#include <cstdlib>

namespace {

constexpr int UnknownId = 0x7fff0000;

void Require(bool value, const char* message) {
    if (value) return;
    std::fprintf(stderr, "%s\n", message);
    std::abort();
}

int APS5_VABI UnexpectedSslCallback(int, unsigned int, void* const*, int, void*) {
    std::abort();
}

void CheckSilentSslStubs(int id) {
    Require(sceHttpsDisableOption(id, 0) == 0, "sceHttpsDisableOption refused zero flags");
    Require(sceHttpsDisableOption(id, UINT32_MAX) == 0, "sceHttpsDisableOption refused all flags");
    auto* unreadable = reinterpret_cast<void*>(static_cast<std::uintptr_t>(0x10));
    Require(sceHttpsSetSslCallback(id, UnexpectedSslCallback, unreadable) == 0, "sceHttpsSetSslCallback refused a callback");
    Require(sceHttpsSetSslCallback(id, nullptr, unreadable) == 0, "sceHttpsSetSslCallback inspected its callback or user argument");
}

}

int main() {
    CheckSilentSslStubs(UnknownId);

    const int context = sceHttpInit_nid_postfix(1, 1, 0x10000);
    Require(context > 0, "sceHttpInit failed");
    const int pattern = sceHttpCreateTemplate(context, "anyps5-test", 2, 0);
    Require(pattern > 0, "sceHttpCreateTemplate failed");
    const int connection = sceHttpCreateConnectionWithURL(pattern, "https://example.com/", 1);
    Require(connection > 0, "sceHttpCreateConnectionWithURL failed");
    const int request = sceHttpCreateRequestWithURL(connection, 0, "https://example.com/", 0);
    Require(request > 0, "sceHttpCreateRequestWithURL failed");

    for (const int id : {pattern, connection, request, UnknownId}) CheckSilentSslStubs(id);

    Require(sceHttpDeleteRequest(request) == 0, "sceHttpDeleteRequest failed");
    CheckSilentSslStubs(request);
    Require(sceHttpDeleteConnection(connection) == 0, "sceHttpDeleteConnection failed");
    Require(sceHttpDeleteTemplate(pattern) == 0, "sceHttpDeleteTemplate failed");
    Require(sceHttpTerm_nid_postfix(context) == 0, "sceHttpTerm failed");

    CheckSilentSslStubs(UnknownId);
    std::puts("HTTP silent SSL stub tests passed");
    return 0;
}

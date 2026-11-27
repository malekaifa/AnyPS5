# relinker

## change [wide-store decoder]

- `--to-rosetta` lowers unaligned 256-bit VMOVUPS, VMOVUPD and VMOVDQU stores whose effective address uses 32-bit addressing through a base or index ([wide-store decoder](../../../core/relinker/codegen/src/x86/WideStoreOperands.cpp)): it materializes the effective address with a truncating 32-bit-addressed LEA and stores both 128-bit halves from the resulting 64-bit address, so the upper half does not wrap at 4 GiB. A Linux x86-64 execution regression now stores 32 bytes from `0xFFFFFFF0` across the 4 GiB boundary; the existing Rosetta-capable execution test covers 32-bit base/index operands. EIP-relative and 32-bit absolute addressing keep their existing handling, and a 32-bit absolute store whose upper-half address crosses the boundary still throws. No affected title has been verified.

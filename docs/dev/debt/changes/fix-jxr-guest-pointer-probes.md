# libSceJxr

## add Functional

- [PKFree_nid_postfix, PKCodecFactory_CreateCodec_nid_postfix, PKCodecFactory_CreateFormatConverter_nid_postfix, PKCreateFactory_nid_postfix, CreateWS_Memory_nid_postfix](../../../core/libs/prx/libSceJxr/Export.cpp) (libSceJxr) - factory IID and pointer slots are copied through guest-memory helpers before host reads or writes; failed probes throw `std::invalid_argument` and preserve `errno`. Allocation outputs are freed if guest copy-out fails.
- [attachISWrite_nid_postfix](../../../core/libs/prx/libSceJxr/src/BitIo.cpp) (libSceJxr) - the bit-I/O state and stream descriptor are copied before use, and the reference offset and updated state are copied back with checked guest writes. Unmapped or read-only guest pointers throw `std::invalid_argument` without changing `errno`.

These are host-safety fallbacks; console behavior for unmapped or read-only guest pointers is unmeasured.

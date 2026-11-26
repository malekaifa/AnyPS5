# libSceJxr

## add Functional

- [PKFree_nid_postfix, CreateWS_Memory_nid_postfix, PKCodecFactory_CreateFormatConverter_nid_postfix, PKCreateFactory_nid_postfix, attachISWrite_nid_postfix](../../../core/libs/prx/libSceJxr/Export.cpp) (libSceJxr) - factory, stream, and bit-I/O guest pointer slots are copied through the guest-memory helpers before host reads or writes; failed probes throw `std::invalid_argument` and preserve `errno`. This is a host-safety fallback; console behavior for unmapped or read-only guest pointers is unmeasured.

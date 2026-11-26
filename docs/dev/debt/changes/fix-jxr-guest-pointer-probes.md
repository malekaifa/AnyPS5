# libSceJxr

## add Functional

- [PKFree_nid_postfix, PKCodecFactory_CreateCodec_nid_postfix, PKCodecFactory_CreateFormatConverter_nid_postfix, PKCreateFactory_nid_postfix, CreateWS_Memory_nid_postfix, attachISWrite_nid_postfix](../../../core/libs/prx/libSceJxr/Export.cpp) (libSceJxr) - factory IID and pointer slots are copied through guest-memory helpers before host reads or writes; failed probes throw `std::invalid_argument` and preserve `errno`. Allocation outputs are freed if guest copy-out fails. This is a host-safety fallback; console behavior for unmapped or read-only guest pointers is unmeasured.

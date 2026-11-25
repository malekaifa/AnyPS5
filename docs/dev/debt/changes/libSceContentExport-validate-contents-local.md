# libSceContentExport

## add Functional

- [sceContentExportValidateContents](../../../core/libs/prx/libSceContentExport/Export.cpp) (libSceContentExport) - firmware 9.20 disassembly checks initialization first, rejects an unsigned count above 256 or a null content-id pointer with a non-zero count as `0x809D3016`, and returns 0 for a zero count without contacting the service. A non-empty list is sent to `SceCntMgrService`; its content-id validity result is not available in the module, so that path remains unimplemented.

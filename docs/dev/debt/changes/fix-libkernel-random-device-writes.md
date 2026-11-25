# libkernel

## change [/dev/random and /dev/urandom]

- [Random device descriptors](../../../core/libs/prx/libkernel/File/src/RandomDevice.cpp) (libkernel) allow `/dev/random` and `/dev/urandom` to be opened with write-only or read/write access. `write`, `pwrite`, `writev` and `pwritev` consume every readable guest byte and return its count; positioned writes ignore the offset, as FreeBSD 11.4's `randomdev_write` consumes its `uio` without using `uio_offset`. Read access still follows the descriptor's open mode. The bytes are discarded instead of being mixed into the host entropy pool, so a guest cannot influence later random reads; FreeBSD feeds them to `randomdev_accumulate`. AIO writes to a random device remain explicitly unimplemented because their console behaviour is not measured. Libc random-device streams still support read modes only. This behavior follows FreeBSD 11.4 and is not measured on a PS5.

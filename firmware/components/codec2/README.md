# codec2 (vendored subset)

Speech codec for Nomiq voice messages (ADR-0004). Subset of [drowe67/codec2](https://github.com/drowe67/codec2)
at commit `310777b1c6f1af0bc7c72f5b32f80f6fd9136962`: only the files needed for modes 3200/2400/1600/1400/1300/1200/700C,
plus the **pre-generated codebook sources** (`codebook*.c`, produced by upstream's `generate_codebook`)
so ESP-IDF needs no host build step.

License: **LGPL-2.1** (see COPYING). Nomiq firmware links it statically; to comply, the release process must
provide the object files / build tree needed to relink with a modified codec2 (tracked in ADR-0004).

Update: rebuild upstream with CMake, copy the same file list + `build/src/codebook*.c` + `build/codec2/version.h`.

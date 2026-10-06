---
description: >-
  shmscope is a live terminal viewer for POSIX shared memory on macOS. Watch a
  segment's bytes change as the writer runs and decode them with Kaitai Struct
  layouts. Install with Homebrew.
---

# shmscope { .visually-hidden }

![shmscope](banner.svg){ width="760" }

A live terminal viewer for POSIX shared memory. Point it at a segment and watch
the bytes change while the writer runs.

Give it a [layout](layouts.md) file and the bytes read as fields instead of hex.

## Install

macOS on Apple Silicon for now.

=== "Homebrew"

    ```sh
    brew tap Ayush272002/tap
    brew trust --formula ayush272002/tap/shmscope
    brew install shmscope
    ```

=== "From source"

    You need CMake 4.2, a C++23 compiler and [uv](https://docs.astral.sh/uv/)
    for Conan.

    ```sh
    uv sync
    cmake --preset release
    cmake --build --preset release
    ```

    The binary ends up in `cmake-build-release/shmscope`.

## Try it

The repo has a small writer that fills a ring buffer. Build from source to get
it:

```sh
./cmake-build-release/ring_writer &
./cmake-build-release/shmscope --layout examples/ring.ksy /shmscope-demo
```

Then type `/field latest.body` to follow the newest record.

## Next

- [Layouts](layouts.md) covers the `.ksy` subset and display formats.
- The [manual](manual.md) lists every option, key and command.

!!! warning "Reads are not synchronised"
    The mapping is read only and shmscope never writes. Reads aren't
    synchronised with the writer, so a value can tear mid write.

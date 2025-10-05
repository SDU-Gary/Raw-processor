# Raw-processor

Lightweight, plugin-driven RAW photo processing engine (C++17/CMake).

Highlights:
- Platform Abstraction Layer for cross-platform dynamic plugin loading
- Plugin-based processing pipeline (PRE_DEMOSAIC, DEMOSAIC, POST_DEMOSAIC_LINEAR, FINALIZE)
- Unified data model for non-destructive edits
- Optional integrations: LibRaw (RAW decode via vcpkg), stb_image_write (PNG/JPG), TinyEXR (EXR)

Quick Start
- Configure and build (with LibRaw via vcpkg toolchain):
  - `cmake -S . -B build -D RAWPROC_WITH_LIBRAW=ON -DCMAKE_TOOLCHAIN_FILE=/path/to/vcpkg/scripts/buildsystems/vcpkg.cmake`
  - `cmake --build build -j`
- Run (grayscale preview + WB/Gamma):
  - `./build/rawproc_cli /path/to/your.RAW`
  - Output: `preview.png`

Layout
- `include/rawproc/` core headers
- `src/` core implementation + PAL
- `plugins/` example plugins (`denoise`, `whitebalance`, `gamma`)
- `apps/` minimal CLI

Notes
- If `stb_image_write.h` / `tinyexr.h` / `CImg.h` are present in `include/rawproc/`, they are auto-detected.
- When LibRaw is enabled, we crop to active sensor area and use camera black/white levels.

License
- TBD

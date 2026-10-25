# sse2neon (vendored)

`sse2neon.h` translates x86 SSE/SSE2 intrinsics to ARM NEON. Used only by the Switch (aarch64) build so the engine's
`#include <xmmintrin.h>` keeps working (`src/platform/switch/include/xmmintrin.h` forwards here); the desktop builds
use the native x86 headers.

- Source: https://github.com/DLTcollab/sse2neon (single header, MIT License).
- Fetched: 2026-10-25 (master). Update by replacing `sse2neon.h`; it is a drop-in header, nothing to build.

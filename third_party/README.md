# Third-party code and assets

| What | Where | Version / provenance | License |
|---|---|---|---|
| Microsoft Detours | `Detours/` (vendored source, built as a static lib by CMake) | github.com/microsoft/Detours commit d644ce94e8c7f7f5a31591577c78134ea3ac1fae | MIT |
| miniaudio | `miniaudio/miniaudio.h` | v0.11.25 (2026-03-04), github.com/mackron/miniaudio | MIT-0 / public domain (dual) |
| doctest | `doctest/doctest.h` | single header | MIT |
| prism speech SDK | `prism-bin/prism-sdk-v0.18.1/` -- only the parts the build uses are committed: `include/`, `windows/x64/dynamic/release/{lib/prism.lib,bin/prism.dll}`, `LICENSES/`, `NOTICE` (the full prebuilt release is 1.1 GB and stays ignored by `.gitignore`) | prebuilt release v0.18.1 from the prism GitHub releases; the build delay-loads `prism.dll` from next to `gdaccess.dll` | MPL-2.0 (see its `NOTICE` and `LICENSES/`) |
| wall-tone loops and review pings | `../assets/audio/` | copied from wotr-access (`assets/audio/walltones/{1,2}/*.wav`, `review_*.wav`), same author | as wotr-access |

`reference/` (iagd, GDCommunityLauncher clones) is ignored by git; it is read-only reference material.

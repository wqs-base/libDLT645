# AGENTS.md

DL/T 645-2007 meter protocol library. Pure C99 static lib, no dynamic memory,
no OS dependency; master (client) + slave (meter), non-blocking core with
blocking wrappers. See `README.md` for the full API tour.

## Build & test

```sh
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build --output-on-failure
```

- Toolchain here is MinGW gcc + Ninja + CMake (`D:\msys64\mingw64\bin`). No MSVC,
  no clang. Link a single test directly when iterating:
  `gcc -std=c99 -Wall -Wextra -Iinclude/dlt645 tests/test_link.c src/*.c -o build/t.exe`
  (excluding `src/dlt645_di_table.inc`, which is `#include`d, not compiled).
- Run one test: `ctest --test-dir build -R test_link --output-on-failure`.
- Keep `-Wall -Wextra` clean; CI is not present, so this is the only gate.

## Critical, non-obvious wiring

- **Include path is `include/dlt645`, not `include`.** Headers `#include` each
  other by bare name (`"dlt645_types.h"`), so every consumer/compile must add
  `-Iinclude/dlt645`. CMake exposes this via `target_include_directories`.
- **`src/dlt645_di_table.inc` is generated — never hand-edit.** It is
  `#include`d by `src/dlt645_di.c`; its struct `dlt645_di_info_t` lives in
  `include/dlt645/dlt645_di.h`. Regenerate with:
  `python tools/gen_di_table.py "docs/多功能电能表通信协议.pdf"`
  (needs PyMuPDF; writes `tools/dlt645_di_catalog.json` + the `.inc`).
- **Tests are standalone `main()` programs** using the `CHECK*` macros and
  `TEST_REPORT` in `tests/test_util.h` (per-TU static counters). No test
  framework. `tests/test_link.c` is the integration test: master↔slave over an
  in-memory FIFO — mirror that pattern for new end-to-end cases.
- `port/dlt645_serial.c` is the only OS-touching file (Windows + POSIX serial,
  line settings 8E1). The core `src/` must stay OS-free.
- `install()` exports a CMake package (`find_package(dlt645)` →
  `dlt645::dlt645`, `dlt645::dlt645_serial`) plus `dlt645.pc`. Consumer-facing
  include style is `#include <dlt645/dlt645.h>`; in-repo sources/tests keep the
  bare `#include "dlt645_xxx.h"` style via `-Iinclude/dlt645`.

## Protocol conventions that bite

- Address is 6 BCD bytes, **low byte first**; `dlt645_addr_parse` reads the
  string as a decimal number where `b[0]` = the two least-significant digits.
  `0xAA` = 缩位 wildcard in high bytes, `999999999999` = broadcast (no reply).
- Data field is offset **`+0x33` on the wire, `-0x33` after**; `dlt645_frame_t.data`
  holds the transformed bytes — call `dlt645_frame_data()` for plain bytes.
- Response ctrl = request `|0x80`; error = `|0xC0`; "more data follows" = `|0x20`.
- `dlt645_master` auto-issues `12H` read-follow when a reply sets the follow-up
  bit (data >200B) and latches the slave's concrete address, since 缩位 responses
  return the real address.
- DI range items (rates 1..63, settlement days, data blocks) are stored as
  **mask/wildcard** entries, not expanded. `dlt645_di_lookup` picks the most
  specific match and falls back by ignoring rate/day; `dlt645_di_format_name`
  appends `(费率N)/(上N结算日)/(数据块)`.

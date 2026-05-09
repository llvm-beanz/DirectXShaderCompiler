# Agent Thoughts: Embedding HLSL headers in dxcompiler

This change adds a portable mechanism for shipping the HLSL headers that
live under `tools/clang/lib/Headers/hlsl/` as compiled-in data inside the
`dxcompiler` library, and teaches the clang preprocessor to satisfy
angle-bracket `#include` directives from that data without consulting the
filesystem.

## Goals (from the task brief)

1. A Python script (`utils/embed_header.py`) that converts a single file
   into a C++ snippet declaring `llvm::StringRef Data = ...;`.
2. CMake build steps so any change to a header under
   `tools/clang/lib/Headers/hlsl/` regenerates its embedded snippet.
3. A second Python script that aggregates the per-file snippets into a
   single source file, namespacing each `Data` variable by the header's
   relative path (slashes/dots → underscores) and exposing the whole
   collection as a `llvm::StringMap<llvm::StringRef>`.
4. A preprocessor change so that `#include <relative/path.h>`
   automatically resolves to the compiled-in data when the filename
   matches one of the embedded headers.

## Design decisions

### Where the embedded data lives

The data is compiled into `clangLex` rather than directly into
`dxcompiler`. `dxcompiler` already links against `clangLex`, so the data
ends up in the dxcompiler library either way, and putting it in
`clangLex` lets the preprocessor reach the data through a normal
function call (`clang::hlsl::getEmbeddedHeaders()`) without needing a
runtime registration hook.

### `embed_header.py` escaping strategy

The script escapes every byte conservatively: backslash, double-quote,
`\t`, `\n`, `\r` get C escape sequences; printable ASCII passes through;
everything else uses **3-digit octal** escapes. Octal escapes consume at
most three octal digits, so they cannot accidentally extend across the
following character. (Hex escapes `\xNN` are dangerous in C++ because
they are open-ended.)

The output puts each input line into its own adjacent `"..."` literal so
that the generated file remains diffable and the C++ compiler doesn't
have to chew through one huge megastring.

The script avoids rewriting an unchanged output file so that downstream
build steps don't get spurious invalidations.

### `generate_hlsl_embedded_headers.py`

Takes `--entry REL=INC` pairs (one per embedded header) and emits a
single C++ source file that:

* `#include`s each generated `.inc` inside a unique namespace derived
  from the header's relative path.
* Defines `clang::hlsl::getEmbeddedHeaders()` returning a static
  `llvm::StringMap<llvm::StringRef>` populated from those namespaces.

The map is built with explicit `M.insert(std::make_pair(...))` calls
rather than the C++11 initializer-list constructor because the LLVM
`StringMap` shipped with this branch of DXC predates that overload (the
build initially failed with "candidate constructor not viable" until we
switched to `insert`).

### CMake integration

The build rules live in `tools/clang/lib/Lex/CMakeLists.txt` (alongside
the consumer of the embedded data). They:

1. Use `file(GLOB_RECURSE ...)` over `*.h` and `*.impl` files in
   `tools/clang/lib/Headers/hlsl/` to enumerate the inputs.  We
   intentionally exclude `LICENSE.txt`, `README.txt`, and editor
   configuration like `.clang-format`.
2. Add one `add_custom_command` per file invoking `embed_header.py`.
3. Add a final `add_custom_command` invoking
   `generate_hlsl_embedded_headers.py` with all the per-file `.inc`
   paths.
4. Add the generated source to the `clangLex` library sources.

The `Python3_EXECUTABLE` variable is the convention already used by the
rest of the DXC build (e.g. `cmake/modules/HCT.cmake` and
`utils/version/CMakeLists.txt`).

### Preprocessor change: fallback semantics

The brief asked for `<...>` includes that match an embedded header to
"use the header from the global data inside the compiler instead of
searching the filesystem." A literal implementation places the embedded
lookup *before* `HeaderSearch::LookupFile`. I tried that first and it
worked end-to-end, but it broke two existing tests
(`SemaHLSL/hlsl/vectors/slice-errors.hlsl` and
`SemaHLSL/hlsl/linalg/linalg-matrix-error.hlsl`) which use
`-I tools/clang/lib/Headers/hlsl` together with `expected-note@<file>:N`
verify directives. Verify resolves the directive's filename via a
**quoted** `Preprocessor::LookupFile`, which still found the on-disk
copy through `-I` and produced a `FileEntry` distinct from the virtual
one used for the angled `#include` — causing every cross-file note to
fail to match.

Rather than rewrite those tests (and force users to lose the ability to
shadow a bundled header by placing a same-named file alongside their
source), I moved the embedded lookup to the **end** of
`Preprocessor::LookupFile`, after the standard `HeaderSearch` and
sub-framework attempts have failed. This means:

* Without any `-I`, the embedded data resolves angled includes of
  bundled headers — the new functional requirement.
* With `-I` pointing at a filesystem copy, the filesystem copy takes
  precedence — preserving every existing test and giving users a clean
  way to override the bundled headers (e.g. for local experimentation).

The synthesized `FileEntry` uses a recognisable virtual filename of the
form `<built-in:hlsl>/<relative path>` so the bundled origin is obvious
in diagnostics and source listings. The contents are wired up via
`SourceManager::overrideFileContents` over the embedded `StringRef`.

### Tests

* `utils/tests/test_embed_header.py` — unit tests for the per-file
  embed script: empty input, plain text, special characters, full
  0..255 byte round trips, per-line literal layout, and idempotent
  output.
* `utils/tests/test_generate_hlsl_embedded_headers.py` — unit tests
  for the aggregator script's helpers (path → namespace, path
  normalisation) and end-to-end output shape.
* `tools/clang/test/SemaHLSL/hlsl/embedded_headers/*.hlsl` — three lit
  tests for the preprocessor change:
  * top-level embedded header (`<enable_if.h>`)
  * nested embedded header (`<dx/linalg.h>`)
  * negative case proving that quoted includes still go through the
    filesystem and fail when the header is unavailable.

## Follow-up: addressing the COPILOT_TODOs

After the initial change above, a second round of feedback (left in
`COPILOT_TODO` comments) was addressed:

### Explicit input list and gating Vulkan headers

`tools/clang/lib/Lex/CMakeLists.txt` no longer uses
`file(GLOB_RECURSE ...)` for the embedded inputs.  Globs are evaluated
at configure time, so a new file would silently be missed until the
next reconfigure.  The list is now spelled out, and the
`vk/` subtree is only embedded when `ENABLE_SPIRV_CODEGEN` is on, so
the DXC-only build doesn't drag in the SPIR-V headers.

### Hand-written cpp + minimal generated includes

The aggregator script no longer emits the entire C++ source file at
build time.  `tools/clang/lib/Lex/HLSLEmbeddedHeaders.cpp` lives in
source control and textually includes two small build-time-generated
`.inc` files:

* `HLSLEmbeddedHeadersDecls.inc` — one
  `namespace <ns> { #include "<path>.inc" }` block per header.
* `HLSLEmbeddedHeadersEntries.inc` — an X-macro list of
  `HLSL_EMBEDDED_HEADER(rel_path, ns)` lines.

`HLSLEmbeddedHeaders.cpp` defines `HLSL_EMBEDDED_HEADER` to insert into
the StringMap before including the entries file, and `#undef`s it
afterwards.  This puts the C++ skeleton (function signature, lambda,
StringMap construction) under code review and limits the generated
content to data that actually depends on the on-disk header set.

### Simplified embedded-header lit tests

The three lit tests under
`tools/clang/test/SemaHLSL/hlsl/embedded_headers/` were rewritten:

* The two positive tests now run `dxc -M %s | FileCheck %s` to check
  that the bundled header appears in the include dependency dump as
  `<built-in:hlsl>/<rel>`, and a second RUN line with `-verify` plus
  `expected-no-diagnostics` confirms the same input compiles cleanly.
  This replaces the previous "preprocess to a temp file and grep"
  approach.
* The negative quoted-include test uses `-verify` with an inline
  `expected-error` comment instead of `not %dxc ... | FileCheck`.

### Retiring `%hlsl_headers`

The lit substitution that pointed at the source `hlsl/` directory is
gone, along with `config.hlsl_headers_dir` and
`HLSL_HEADERS_DIR`.  Every test that depended on it was updated to
rely on the embedded headers instead — most just needed `-I
%hlsl_headers` removed.  Two collateral fixes were necessary:

* `tools/clang/lib/Headers/hlsl/vk/khr/cooperative_matrix.h` and
  `cooperative_matrix.impl` used quoted `#include "..."` for sibling
  bundled headers.  Quoted lookups skip the embedded fallback by
  design (so the negative `embedded_header_quoted.hlsl` test still
  holds), so these intra-bundle includes were switched to angled
  form.  The same conversion was applied in
  `tools/clang/test/CodeGenSPIRV/convert.selector.hlsl`, which
  textually included `vk/opcode_selector.h` in quoted form.
* `VerifyDiagnosticConsumer`'s `expected-note@<file>:line` directives
  resolve `<file>` through `Preprocessor::LookupFile` with
  `isAngled=false`.  When that quoted lookup fails (which is now the
  common case for bundled headers), it retries with `isAngled=true`,
  letting the embedded fallback satisfy the directive's file lookup
  with the same virtual `<built-in:hlsl>/<rel>` `FileEntry` that the
  actual `#include` resolved to.

### Verification

Reconfigured with `-C cmake/caches/PredefinedParams.cmake` and ran
`ninja check-all`.  4599 expected passes; no new failures.

## Follow-up: explicit `-I`-overrides-embedded test

A later request asked for explicit test coverage of the workflow where
`-I` points at a directory containing a header that shadows one of the
compiled-in HLSL headers, and an angle-bracket `#include` resolves to
the on-disk file (so users can inspect or modify the bundled headers).
The fallback-after-`HeaderSearch` ordering in
`Preprocessor::LookupFile` already guarantees this behaviour, but no
test was exercising it.

Added:

* `tools/clang/test/SemaHLSL/hlsl/embedded_headers/Inputs/enable_if.h`
  — an overlay copy of the bundled header that defines a unique
  `HLSL_OVERLAY_ENABLE_IF` macro so the test can prove which version
  was actually included.
* `tools/clang/test/SemaHLSL/hlsl/embedded_headers/embedded_header_include_path.hlsl`
  — runs `dxc` twice with `-I %S/Inputs`:
  1. `-M ... | FileCheck` confirms the dependency dump references the
     on-disk overlay path and **does not** mention the
     `<built-in:hlsl>/...` virtual filename used for embedded data.
  2. `-verify` plus an `#error` guarded on `HLSL_OVERLAY_ENABLE_IF`
     confirms the on-disk overlay was the version actually consumed by
     the preprocessor.

The `Inputs/` subdirectory is automatically excluded from lit
discovery by `tools/clang/test/lit.cfg`'s standard `excludes` list,
and `.h` is not in `config.suffixes`, so the overlay is never picked
up as a test of its own.

Verified by running the embedded-headers lit suite (4/4 PASS) and the
broader `SemaHLSL/` lit suite (259 PASS + 1 expected failure, no
regressions).

## Follow-up: Windows path separators in embedded-header includes

A reviewer asked for test coverage to ensure that an angle-bracket
`#include` written with Windows-style separators (backslashes) still
resolves to the compiled-in HLSL header.

The embedded-header map is keyed on POSIX-style relative paths (e.g.
`dx/linalg.h`) generated from the `tools/clang/lib/Headers/hlsl/`
source tree at build time.  The original lookup in
`Preprocessor::LookupFile` looked up the raw `Filename` string from the
`#include` directive directly in that map, so an include such as
`#include <dx\linalg.h>` missed the entry and reported the header as
not found, even though `<dx/linalg.h>` resolved correctly.

### Fix

In `tools/clang/lib/Lex/PPDirectives.cpp`, before consulting the
embedded-header map, copy `Filename` into a `SmallString` and replace
any `\\` with `/`.  Use the normalised string both for the `StringMap`
lookup and for the synthesised virtual `<built-in:hlsl>/...` filename
plus the `RelativePath` output, so dependency dumps and diagnostics
show a single canonical (forward-slash) path regardless of how the
include was spelled.  This intentionally only affects the embedded
fallback; the existing on-disk `HeaderSearch::LookupFile` path is
unchanged, since on the POSIX builds DXC supports it would already
treat backslashes as part of the literal filename.

### Test

Added `tools/clang/test/SemaHLSL/hlsl/embedded_headers/embedded_header_windows_path.hlsl`
mirroring `embedded_header_nested.hlsl` but spelling the include as
`#include <dx\linalg.h>`.  The test runs `dxc` twice:

1. `-verify` (with `expected-no-diagnostics`) confirms Sema accepts
   the program, i.e. the include resolved through the embedded
   fallback.
2. `-M ... | FileCheck` asserts that the dependency dump records the
   canonical `<built-in:hlsl>/dx/linalg.h` path — proof that the
   normalisation is reflected in the virtual filename emitted by the
   preprocessor, not just in the lookup key.

Verified: the embedded-headers lit suite is now 5/5 PASS, the broader
`SemaHLSL/` suite is unchanged, and `ninja check-clang` reports
2973 expected passes / 7 expected failures / 0 regressions on the
existing `build-assert` configuration.

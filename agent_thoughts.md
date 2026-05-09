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

### Verification

Configured the build using `-C cmake/caches/PredefinedParams.cmake`
(via the existing `build-rel` directory) and ran `ninja check-all`. All
4599 expected-pass lit tests pass; no new failures were introduced.

## Commit layout

The change is split into small commits:

1. `Add embed_header.py utility for embedding files as StringRef` —
   just the per-file script and its unit tests; reviewable in
   isolation.
2. `Embed HLSL headers into clangLex via generated source` — the
   aggregator script, the public header, and the CMake build rules.
3. `Resolve angled HLSL header includes from compiled-in data` — the
   preprocessor change and lit tests.
4. (this file) — agent thoughts.

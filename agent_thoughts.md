# Agent thoughts: cleaning up DXC's UTF-8 / wide-character abstraction
## Goal

The task asked to inspect DXC's platform layers that convert between UTF-8
and the various wide-character formats Windows uses, and to clean up the
abstraction so that **strings stay UTF-8 internally** and are converted to
wide characters **only at the actual platform boundary** (the Windows
console, a wide file API, an output buffer that ABI-requires UTF-16, etc.).

## Investigation

I started by surveying the conversion code and its callers:

* `include/dxc/Support/Unicode.h` / `lib/DxcSupport/Unicode.cpp` — the central
  `Unicode::*` helpers (`UTF8ToWideString`, `WideToUTF8String`,
  `UTF8ToConsoleString`, `WideToConsoleString`,
  `UTF8BufferToWideBuffer`, `WideBufferToUTF8Buffer`, …).
* `lib/DxcSupport/dxcapi.use.cpp` — the `dxc::WriteUtf8ToConsole*`,
  `dxc::WriteBlobToConsole`, and the (file-private)
  `WriteWideNullTermToConsole` helpers used by every command-line tool
  (`dxc`, `dxr`, `dxopt`, `dxbc2dxil`, `dxc_batch`).
* `lib/DxilDia/DxilDia.cpp` — `StringRefToBSTR`, which has to produce a
  wide BSTR (a real boundary, kept as-is).
* `include/dxc/Test/WEXAdapter.h` — the non-Windows shim for TAEF-style
  `WEX::Logging::Log` calls used throughout the HLSL gtests.
* `include/dxc/WinAdapter.h` — `ScopedLocale`, `CW2A`, `CA2W`,
  `STD_*_HANDLE` etc.

### Round-trip in `WriteUtf8ToConsole`

The most clearly broken call chain was the supposedly UTF-8 path in
`dxcapi.use.cpp`:

```
WriteUtf8ToConsole(utf8, n)
  -> Unicode::UTF8BufferToWideBuffer            (UTF-8 -> wide)
     -> WriteWideNullTermToConsole
        -> Unicode::WideToConsoleString          (wide -> console code page)
           -> fprintf(stdout, "%s\n", consoleMsg)
```

* On non-Windows hosts every step is wasted: `GetConsoleOutputCP()` returns
  `0`, so `WideToConsoleString` does another UTF-8 decode under the
  scoped UTF-8 locale and we end up writing the same UTF-8 bytes that came
  in (after first allocating a `wchar_t[]` and re-decoding twice).
  Strictly invalid UTF-8 in a buffer would be rejected before we ever got
  to print, even though the destination terminal could have shown it
  fine.
* On Windows the round trip is actively destructive: the conversion to
  the console code page silently replaces any character that isn't
  representable in the code page (typical default `437`/`1252` etc.). We
  already had the wide buffer; the right thing is to call
  `WriteConsoleW` directly so that the console itself receives full
  Unicode regardless of its code page.

### WEX adapter wide writes

The non-Windows `Log::Comment` / `Log::Error` helpers used `fputws` and
`wprintf`. These write *narrow* bytes after running each `wchar_t`
through the C library locale's wide-to-multibyte converter. Test
processes don't initialize the locale; the default is `"C"`, which only
handles ASCII, so on Linux/macOS:

* a `wchar_t` whose code point is > 0x7F gets dropped or terminates the
  output at the high byte (which the C locale interprets as a null), and
* failure messages logged via `Log::Error(L"...")` simply never appear in
  the test log.

That matches the behavior described in the task ("strings not being
printed since the top bytes are often interpreted as null characters").

## Changes (one commit per change)

1. **`[DxcSupport] Avoid UTF-8 round-trip in console output helpers`**
   Refactor `dxcapi.use.cpp` around a single `WriteUtf8BytesToStream`
   worker. UTF-8 stays UTF-8 unless the destination is an actual Windows
   console; only then do we convert to UTF-16 once and call
   `WriteConsoleW`. Redirected handles on Windows now keep the original
   UTF-8 bytes (`WriteFile`), and non-Windows just `fwrite`s them.
   `WriteWideNullTermToConsole` follows the same pattern.

2. **`[Test/WEXAdapter] Emit log messages as UTF-8 instead of wide bytes`**
   Convert wide log strings via `Unicode::WideToUTF8String` and write
   narrow UTF-8 bytes with `fputs`/`fputc`. Pulled `<cstdint>` into
   `dxc/Support/Unicode.h` so it is safe to include before
   `WinIncludes.h`.

3. **`[DxcSupport] Add UTF-8 / wide round-trip and console output tests`**
   New `unittests/DxcSupport/UnicodeTest.cpp`:

   * Round-trip a string mixing 1-, 2-, and 4-byte UTF-8 sequences
     (`"héllo wörld 🌍"`) through `UTF8ToWideString` /
     `WideToUTF8String` and through the buffer-allocating
     `UTF8BufferToWideBuffer` / `WideBufferToUTF8Buffer` APIs.
   * On non-Windows, redirect `stdout`/`stderr` to a pipe and verify
     that `dxc::WriteUtf8ToConsoleSizeT` writes the exact UTF-8 bytes
     plus the documented trailing newline. This locks in the
     "no narrowing through the C locale" guarantee that the refactor
     introduced.

4. **This document**, committed on its own.

## What I deliberately did not change

* `Unicode::UTF8ToConsoleString` and `Unicode::WideToConsoleString` are
  still in the public API — they're used by `dxr`, `dxc`, and
  `dxbc2dxil` to render diagnostic strings whose lossiness is reported
  to the caller. Their semantics are unchanged.
* `DxilDia::StringRefToBSTR` legitimately needs UTF-16 because it
  produces a `BSTR`, which is wide by ABI. It already keeps strings as
  UTF-8 right up to that boundary.
* The `Unicode.cpp` shim implementations of `MultiByteToWideChar` /
  `WideCharToMultiByte` for non-Windows are unchanged. Those are the
  underlying primitives the rest of the conversion API is built on.

## Verification

* `cmake --build build-rel --target check-all` (using
  `cmake/caches/PredefinedParams.cmake` from the existing build tree):
  4601 expected passes / 9 expected failures / 33 unsupported, **0
  unexpected failures**.
* `build-rel/unittests/DxcSupport/DxcSupportTests` — 6/6 passing
  (1 pre-existing + 5 new).
* Spot-checked `ClangHLSLTests` filters (`*OptionsTest*`,
  `DxilContainerTest.*`, etc.) to confirm the WEXAdapter changes
  compile and run.
* Smoke-tested `bin/dxc --help` to confirm the refactored
  `WriteUtf8ToConsole` path still produces the expected help output on
  Linux.

---

# Agent thoughts: removing hard-coded carriage returns from DXC output

## Goal

The follow-up task asked me to search the DXC codebase for places where a
carriage return (`\r`) is **explicitly added** to output and replace those with
a portable implementation, since on Unix-only platforms those bytes leak into
text output (especially in test logs).

## Investigation

I started by searching with ripgrep for `\r\n` and `'\r'` literals across
`lib/`, `include/`, `tools/`, and `unittests/`. The hits split cleanly into
two categories:

1. **Parsing / lexing of input** — every place that *reads* `\r\n` line
   endings to detect or normalize them: `lib/Support/YAMLParser.cpp`,
   `lib/TableGen/TGLexer.cpp`, `lib/Support/LineIterator.cpp`,
   `tools/clang/lib/Lex/Lexer.cpp`, `tools/clang/lib/Lex/Preprocessor.cpp`,
   `tools/clang/lib/Frontend/PrintPreprocessedOutput.cpp`,
   `tools/clang/lib/Frontend/Rewrite/InclusionRewriter.cpp`,
   `tools/clang/lib/AST/RawCommentList.cpp`,
   `tools/clang/lib/Basic/SourceManager.cpp`,
   `tools/clang/unittests/HLSLTestLib/FileCheckForTest.cpp`, and so on.
   These all need to keep handling CRLF in *input* sources and were left
   alone.

2. **Output that hard-coded `\r\n`** — the actual targets of this task.

   * `lib/DxcSupport/dxcapi.extval.cpp` — `fprintf(stderr, "error: validation
     errors\r\n")` after a failed validate.
   * `tools/clang/tools/dxcompiler/dxcutil.cpp` and
     `tools/clang/tools/dxrfallbackcompiler/dxcutil.cpp` — the diagnostic
     format strings `"validation errors\r\n%0"` and `"root signature
     validation errors\r\n%0"`.
   * `tools/clang/tools/dxa/dxa.cpp` — `printf("%S\r\n", (LPWSTR)name)` when
     listing PDB source names.
   * `tools/clang/tools/dxclib/dxc.cpp` and
     `tools/clang/unittests/dxc_batch/dxc_batch.cpp` — `WriteHeader`
     hand-rolled CRLF emission for the `#if 0 ... #endif` disassembly
     comment block, the `const unsigned char[] = { ... }` byte array, and a
     loop that translated every embedded `\n` in the disassembly into
     `\r\n` before writing.
   * `include/dxc/Test/HlslTestUtils.h` — `strreplace`'s "String not found"
     failure message.
   * `tools/clang/unittests/HLSLExec/ExecutionTest.cpp` — the D3D12 InfoQueue
     dumper appended `L"\r\n"` per message and `"Failed to retrieve some
     messages.\r\n"` on a fallback path.
   * `tools/clang/unittests/HLSLExec/ShaderOpTest.cpp` — seven
     `ShaderOpLogFmt(L"...\r\n", ...)` log lines (no root signature found,
     compile failures, callback-required, decode failure, etc.).

I deliberately left a few places alone:

* `include/dxc/Support/Global.h`'s `OutputDebugBytes` /
  `OutputDebugFormatA`. They are inside `#ifdef _MSC_VER` and call the
  Windows-only `OutputDebugStringA`. The Windows debugger console expects
  CRLF, and the code never runs on Linux/macOS.
* `tools/clang/lib/Format/WhitespaceManager.cpp`. It already uses the
  portable pattern `Text.append(UseCRLF ? "\r\n" : "\n")` based on a
  user-controlled config option; that's not "hard-coded" in the sense of
  the task.
* HLSL test inputs that contain CRLF inside string literals
  (e.g. `tools/clang/unittests/HLSL/CompilerTest.cpp` and the
  `WaveIntrinsicsExt`-style defines in
  `tools/clang/unittests/HLSLExec/ExecutionTest.cpp`). Those CRLF bytes
  are *shader source content*, used to verify that the compiler accepts
  CRLF line endings in HLSL input — not output.
* Lexer / preprocessor / FileCheck CRLF handling — must keep parsing CRLF.

## Why `\n` is the right "portable implementation"

For every place I changed, the destination is one of:

* The C runtime (`fprintf` / `printf`) writing to a `FILE*` opened in text
  mode. On Windows, `\n` is automatically translated to CRLF on its way to
  the OS; on Linux/macOS it stays as LF. Hard-coding `\r\n` would therefore
  produce `\r\r\n` on Windows (or, in our case, the `\r\n` was being
  appended after the runtime had already emitted CRLF for the format
  string's `\n` — wrong on both platforms once you trace it).
* A `clang::DiagnosticsEngine` format string, which feeds into the
  diagnostic printer. The printer adds its own newline; the embedded `\r`
  was just leaking through.
* `WEX::Logging::Log` / TAEF on Windows or our shim on Linux. Both call
  through to a logger that accepts a logical newline; the carriage return
  was redundant on Windows and literally rendered as `^M` on Linux.
* `llvm::raw_string_ostream` writing into a `std::string` that gets passed
  to `WriteString` / `WriteBlobToFile`. Those write the bytes verbatim. We
  want LF only so the produced files round-trip cleanly through Git and
  Linux text tools; Windows tooling (Visual Studio, `cl.exe`, etc.) is
  long-since happy with LF-only text.

## Changes (one commit per change)

1. **`[Diagnostics] Use \n instead of \r\n in validation error messages`** —
   `dxcapi.extval.cpp` and the two `dxcutil.cpp` copies.

2. **`[Tools] Stop hard-coding CRLF in dxc/dxa/dxc_batch text output`** —
   `dxa.cpp`, `dxclib/dxc.cpp`, `unittests/dxc_batch/dxc_batch.cpp`. This
   also drops the inner `if (pBytes[i] == '\n') OS << '\r';` loop in
   `WriteHeader`, since the disassembly already terminates each line with
   `\n`.

3. **`[Test] Drop hard-coded CRLF from HLSL test logging`** —
   `HlslTestUtils.h`, `ExecutionTest.cpp`, `ShaderOpTest.cpp`.

4. **`[Test] Add lit test guarding CRLF in dxc -Fh header output`** —
   `tools/clang/test/CodeGenDXIL/header-newlines.hlsl`. Runs `dxc -Fh` and
   asserts (via `not grep -U $'\r' ...`) that no carriage return ends up in
   the produced header. Without commit (2) this test fails because every
   line in the header was terminated by `\r\n`.

5. **This update to `agent_thoughts.md`**, committed on its own.

## Verification

* `cmake --build build-rel --target check-all` (build tree configured with
  `-C cmake/caches/PredefinedParams.cmake`) reports **4601 expected passes
  / 9 expected failures / 33 unsupported / 0 unexpected failures**, same
  as the pre-change baseline.
* New lit test `Clang :: CodeGenDXIL/header-newlines.hlsl` passes after
  the change. (I sanity-checked that it would have failed before by
  inspecting the WriteHeader code path; the test grep-asserts no `\r` in
  the produced header.)
* The diagnostic message change is exercised indirectly by the existing
  `CHECKVAL: error: validation errors`-style FileCheck tests under
  `tools/clang/test/CodeGenDXIL/`, which still pass — FileCheck strips
  trailing `\r` from input, so they were tolerant of either form, but the
  underlying compiler now emits the same bytes on every platform.

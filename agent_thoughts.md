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

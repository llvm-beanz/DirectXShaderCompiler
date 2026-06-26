# Agent Thoughts: Removing legacy HLSL effects syntax in HLSL 202x

## Goal
DXC's parser contains special cases that *silently ignore* the legacy HLSL
effects syntax (emitting `-Weffects-syntax` warnings and skipping tokens).
Under HLSL 202x the effects syntax is fully removed, so these workarounds
should be disabled for `HLSLVersion >= v202x`, letting the compiler produce its
natural diagnostics (errors). Add tests proving the diagnostics appear.

## Investigation
Searched for the effects handling and found it spread across the parser and
semantic analysis:

Parser phase (`tools/clang/lib/Parse`):
1. `Parser.cpp` `case tok::kw_technique` — skips `technique { ... }`.
2. `ParseDecl.cpp` `= sampler_state { ... }` — skips the assignment.
3. `ParseDecl.cpp` "skip initializer of effect object" when
   `D.isInvalidType()`.
4. `ParseDecl.cpp` effect state block `{ ... }` after a declarator.
5. `ParseDecl.cpp` effect annotation `< ... >` after a declarator.

Sema phase (`tools/clang/lib/Sema/SemaHLSL.cpp`):
6. `AddObjectTypes` registers the deprecated effect object *type names*
   (`texture`, `PixelShader`, `BlendState`, ...). `DiagnoseHLSLDecl` then warns
   `warn_hlsl_effect_object` and invalidates the declarator.

The HLSL language version is available everywhere via
`getLangOpts().HLSLVersion` and compared against `hlsl::LangStd::v202x`
(`include/dxc/Support/HLSLVersion.h`). Many existing sites already use this
pattern.

## Approach
Gate the parser effects skips (sites 1, 2, 4, 5) on
`HLSLVersion < hlsl::LangStd::v202x`. For 202x and later the special cases are
not taken, so the offending tokens flow into normal parsing and produce natural
diagnostics:
- `technique` -> "expected unqualified-id"
- effect annotation `< ... >` -> "expected ';' after top level declarator"
- effect state block `{ ... }` -> "expected ';' after top level declarator"
- `= sampler_state { ... }` -> "expected expression"

For the Sema phase (site 6) I chose to simply not register the deprecated
effect object type names in 202x. Using one then yields a natural
"unknown type name" diagnostic, which is cleaner and more consistent with the
parser changes than keeping the warning. The fixed-size `std::array`
`m_objectTypeDeclsMap` still has every slot initialized (the skipped entries
are set to `{nullptr, 0}`) so the sorted lookup in `FindObjectBasicKindIndex`
stays well-defined.

## Decision about site 3 (skip initializer of invalid effect object)
Initially I also gated site 3, but `check-all` revealed a regression:
`HLSLFileCheckLit/hlsl/auto/auto-no-pointer.hlsl` (compiled with `-HV 202x`)
started emitting an extra "operator is not supported" error for `&x` in
`auto* ptr = &x;`. Site 3 is in fact a *generic* recovery for any
invalid-typed declarator with an initializer (it suppresses cascading errors),
not something specific to effects syntax. Gating it changed unrelated error
recovery, so I reverted that hunk and left site 3 applying to all versions.
This also keeps the new 202x diagnostics clean (single error per construct).

## Testing
Added `tools/clang/test/SemaHLSL/effects-syntax-202x.hlsl` (run for both
`lib_6_3` and `ps_6_0`, `-HV 202x`, `-verify`) covering:
- parser phase: technique, effect annotation, effect state block,
  sampler_state assignment;
- sema phase: every deprecated effect object type name now being unknown;
- a regression guard that `register()` annotations still parse and a normal
  entry point still compiles.

The pre-existing `SemaHLSL/effects-syntax.hlsl` (default HLSL version) continues
to pass unchanged, proving older language modes keep the deprecation warnings.

## Verification
Configured/built with `cmake/caches/PredefinedParams.cmake` and ran the
`check-all` target. Result: 4627 expected passes, 9 expected failures,
33 unsupported, 0 unexpected failures.

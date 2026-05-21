# Agent Thoughts: `auto` return-type deduction in HLSL

## Goal

Add C++14-style function return-type deduction using `auto` to HLSL,
enabled from HLSL 2016 onward and reporting an "HLSL 202x extension"
warning when used in language modes earlier than 202x.

## Existing state of the world

Variable-level `auto` was added to HLSL in a previous change (PR #8452,
commit d9c0a26b7), under the 202x language mode. That work:

* Reserved `auto` as an HLSL keyword in HLSL 2015 only; from 2016+ the
  token is admitted as a C++-style type specifier.
* Wired the parser path in `Parser::ParseDeclarationSpecifiers` so the
  C++11 `TST_auto` branch is taken when the language is HLSL 2016+.
* Added the `ext_auto_type_specifier` warning text with an "HLSL 202x"
  variant, fired from `DeclSpec::Finish` whenever the language mode is
  HLSL but earlier than 202x.
* Removed an HLSL-specific assertion in `Sema::AddInitializerToDecl`.

That means `auto x = …;` already works for HLSL 2016+, with the
extension warning before 202x. The diagnostic infrastructure for
multi-version `auto` is already in place.

What was *not* enabled was using `auto` as the **return type** of a
function. The Clang return-type deduction code path is gated on
`LangOpts.CPlusPlus14` in several places, and HLSL never sets that flag.
So the goal was to open up those gates for HLSL 2016+.

## Tracing the C++14 deduced-return code path

The relevant `getLangOpts().CPlusPlus14` checks live in:

1. `Sema/SemaType.cpp` — `GetFullTypeForDeclarator`:
   * `~ line 2662`: the "auto not in an allowed context" diagnostic
     loop. The early-exit `!CPlusPlus11 || !D.isFunctionDeclarator()`
     was preventing HLSL from skipping the function-prototype check,
     resulting in the `err_auto_not_allowed` "in function return type"
     error.
   * `~ line 3753`: the `err_auto_missing_trailing_return` /
     `err_deduced_return_type` diagnostics that fire when `auto` is
     used as the outermost return type without a trailing return type
     and the language is not C++14.

2. `Sema/SemaStmt.cpp` — `ActOnReturnStmt`:
   * `~ line 3173`: where, in C++14, the actual deduction is triggered
     via `DeduceFunctionTypeFromReturnExpr` when the current function
     has an undeduced return type.

3. `Sema/SemaDecl.cpp` — `ActOnFunctionDeclarator` and `ActOnFinishFunctionBody`:
   * `~ line 7466`: emits `err_auto_fn_virtual` when an undeduced
     return is combined with `virtual`.
   * `~ line 7471`: handles dependent contexts (templates / friend
     declarations) by substituting `auto` with `DependentTy`.
   * `~ line 10873`: at the end of a body with no return statements,
     substitutes `void` for `auto` (or emits
     `err_auto_fn_no_return_but_not_auto` when the return type is
     `auto`-of-something-else).

4. `Sema/SemaExpr.cpp` — `CanUseDecl` / `DiagnoseUseOfDecl`:
   * `~ lines 66 and 369`: trigger return-type deduction when a
     function with an undeduced return type is referenced (so call
     sites that follow the body get a deduced type, and call sites
     that precede the body produce
     `err_auto_fn_used_before_defined`).

The fix is to widen each of these `CPlusPlus14` checks to also accept
`HLSL && HLSLVersion >= v2016`. I considered factoring this into a
single helper in `Sema.h`, but the existing HLSL changes in these files
already inline the same `LangOpts.HLSL && LangOpts.HLSLVersion >= …`
pattern, so I kept the changes inline for consistency and minimal
surface area.

## Diagnostics and warnings

The pre-existing `ext_auto_type_specifier` warning fires from
`DeclSpec::Finish` whenever a `TST_auto` declaration is seen in HLSL
before 202x. That applies equally to `auto x = …;` and `auto f() { … }`
because both set `TST_auto` on the declaration's `DeclSpec`. No new
diagnostic was needed for the 202x extension warning, and no additional
HLSL-specific reject was needed for misuse — the Clang error machinery
(`err_auto_fn_deduction_failure`, `err_auto_fn_different_deductions`,
`err_auto_fn_used_before_defined`, `err_auto_fn_no_return_but_not_auto`,
`err_auto_fn_return_init_list`, etc.) all fire correctly once the
language gates are opened.

I considered also opening the `LambdaExprContext` / `TrailingReturn` /
`ConversionId` gates at the top of `GetFullTypeForDeclarator`, but
those are all about C++ constructs that HLSL does not support
(lambdas, trailing return types, conversion functions), so they would
be unreachable in HLSL. Better to leave them alone for now.

## Tests

* `tools/clang/test/HLSLFileCheckLit/hlsl/auto/auto-return-type.hlsl`
  — DXIL codegen test that verifies an `auto`-returning function
  with int, float, vector and void deductions emits an LLVM function
  with the matching return type, and that the extension warning fires
  in the default (pre-202x) language mode.
* `tools/clang/test/HLSLFileCheckLit/hlsl/auto/auto-return-errors.hlsl`
  — Diagnostics test for inconsistent deduction between return
  statements, use-before-definition through forward declaration, and
  use-before-definition through recursion.
* `tools/clang/test/HLSLFileCheckLit/hlsl/auto/auto-return-extension-warning.hlsl`
  — Confirms that HLSL language modes 2016 through 2021 produce the
  "HLSL 202x extension" warning when `auto` is used as a return type.
* `tools/clang/test/CodeGenSPIRV/fn.auto.return.hlsl` — SPIR-V
  codegen test that verifies `OpFunction` signatures match the
  deduced return types when targeting SPIR-V via `-spirv`.

I initially included a test for `return { 1, 2 };` triggering
`err_auto_fn_return_init_list`, but braced initializer lists are
flagged by an earlier HLSL diagnostic ("generalized initializer lists
are incompatible with HLSL") so the auto-specific diagnostic is never
reached. I dropped that case from the diagnostics test.

## Verification

* Rebuilt `dxc` after each source change.
* Ran the HLSL `auto` lit suite and the SPIR-V `var.auto.*` lit tests
  after each substantive change; existing tests continued to pass.
* Final `ninja check-all` from `build-rel/`:
  4614 expected passes, 9 expected failures, 33 unsupported, no
  unexpected failures.
* Ran `ClangSPIRVTests` to spot-check SPIR-V unit tests: 108 passed.

## Commit layout

1. `[202x] Support auto return type deduction (C++14-style)` —
   the Sema changes that open the C++14 deduction code paths for
   HLSL 2016+.
2. `[202x] Add tests for auto return type deduction` — the four new
   lit tests covering DXIL codegen, SPIR-V codegen, diagnostics, and
   the per-version extension warning.
3. `Record agent thoughts for auto return type deduction work` —
   this file.

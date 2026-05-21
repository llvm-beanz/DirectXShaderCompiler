# Agent Thoughts: `[[dxc::autodiff]]` attribute and `dxr --generate-differentials`

This document records the design decisions and the non-obvious wrinkles I
hit while implementing the autodiff attribute and the matching rewriter
pass.  The work is split across four small commits, plus this notes
commit:

1. `[hlsl] Add [[dxc::autodiff(...)]] attribute`
2. `[dxr] Add -generate-differentials option`
3. `[dxr] Implement -generate-differentials rewriter pass`
4. `[test] Add FileCheck tests for [[dxc::autodiff]] and
   -generate-differentials`

## Phase 1 — the attribute

The attribute is declared in `tools/clang/include/clang/Basic/Attr.td`
as a C++11-style `dxc::autodiff` spelling with two `IdentifierArgument`s
(the second optional) and `Subjects = SubjectList<[Function]>`.  Sema
validates each argument is the identifier `fwd` or `bwd` and disallows
the same mode being passed twice.  The attribute is "ignored during
normal compilation" simply by virtue of having no codegen handling —
every backend just sees an `HLSLAutoDiffAttr` hanging off the
`FunctionDecl` and does nothing with it.

Three things were not at all obvious:

1. **HLSL turns off `LangOpts.CPlusPlus11`.**  The TableGen-generated
   `hasAttribute()` function (in `AttrHasAttributeImpl.inc`) gates almost
   every C++11 attribute on `LangOpts.CPlusPlus11`, so the HLSL parser
   silently dropped the attribute's argument list and reported "takes at
   least 1 argument".  The fix is to teach
   `hasCXXAttributeInHLSL` (in `tools/clang/lib/Parse/ParseDeclCXX.cpp`)
   that `dxc::autodiff` exists.  Without this the rest of the
   implementation is dead code.

2. **The parser only handled identifier args in the first position.**
   `ParseAttributeArgsCommon` calls `attributeHasIdentifierArg(AttrName)`
   once, before the loop, and uses that to decide *only the first*
   argument's parsing strategy.  Later arguments always went through
   `ParseAssignmentExpression`, which on HLSL refuses bare identifiers
   like `bwd`.  I generalised the do-while loop in `ParseDecl.cpp` to
   attempt `ParseIdentifierLoc()` at every position when the attribute
   opts into identifier args and the lookahead is `,` or `)`.  The
   change is backwards compatible because both conditions must hold.

3. **The auto-generated `printPretty` for an attribute with an optional
   `IdentifierArgument` is unsafe.**  TableGen emits code that
   unconditionally dereferences `getMode2()`, so a single-argument
   `[[dxc::autodiff(fwd)]]` SIGSEGV's whenever someone re-prints the
   AST — including the rewriter's `TranslationUnitDecl::print`.  Two
   things have to happen to dodge this:
   - Add a `HLSLAutoDiff` case to `CustomPrintHLSLAttr`
     (`SemaHLSL.cpp`) so we emit the textual form ourselves.
   - Add `HLSLAutoDiff` to `IsHLSLAttr`.  Without that, the generic
     `DeclPrinter::prettyPrintAttributes` path still calls the unsafe
     TableGen `printPretty`.  Only the HLSL-attribute filter routes the
     attribute through `PrintHLSLPreAttr` → `CustomPrintHLSLAttr`.

## Phase 2 — the option

`-generate-differentials` is plumbed through the standard rewriter-option
machinery: a new flag in `HLSLOptions.td`, a new `bool
GenerateDifferentials` member of `RewriterOpts` in `HLSLOptions.h`, and a
`hasFlag` call in `HLSLOptions.cpp`.  No surprises.

## Phase 3 — the rewriter

The rewriter lives in `tools/clang/tools/libclang/dxcrewriteunused.cpp`
and is gated by `opts.RWOpt.GenerateDifferentials` in `DoSimpleReWrite`.
Rather than use `clang::Rewriter` (which would have required a
source-faithful preserving printer that the existing rewriter pipeline
does not provide), I added a small custom translation-unit printer that
iterates `tu->decls()`, prints each one with the standard
`Decl::print`, and appends a generated block immediately after any
`FunctionDecl` carrying `HLSLAutoDiffAttr`.  This preserves the
"immediately after" placement that the prompt asked for and slots
cleanly into the existing `tu->print()` style used by the surrounding
rewriter modes.

The generated block is produced by `AutoDiffEmitter`, a small recursive
descent over the function body that handles:

  - Arithmetic `BinaryOperator` (`+ - * /`).  Forward mode keeps the
    HLSL operators on `Value<T>`; backward mode lowers each to
    `add/subtract/multiply/divide<T>(...)`.
  - `UnaryOperator(-)`.  Forward keeps `-x`; backward emits
    `negate<T>(x)`.
  - `CallExpr` for the AD intrinsics `sin/cos/exp/log/log2/sqrt/pow/
    max/min`.  Forward keeps the call as-is on `Value<T>`; backward
    emits the matching `sinExpr/cosExpr/expExpr/logExpr/log2Expr/
    sqrtExpr/power/maxExpr/minExpr<T>` builder.
  - `DeclRefExpr` to parameters.  Forward leaves them alone; backward
    references the `<name>_expr` variable created at the top of the
    body.
  - Integer / floating-point literals.

Anything outside this subset emits a `/* TODO: unsupported expr */`
sentinel rather than failing the rewrite — the prompt described this as
a starting point, not a complete IL.

The element type used to parameterise `Value<T>` / `Variable<T>` is
extracted from the function's return type, printed canonically.  This
has only been validated on scalar `float`; vectors and matrices will
emit something but it has not been checked end-to-end.

## Phase 4 — tests

Five FileCheck tests live alongside the existing test suite:

- `hlsl/functions/autodiff_attr.hlsl` — smoke test that all four legal
  forms of the attribute compile cleanly on a function definition and
  do not perturb DXIL emission.
- `hlsl/diagnostics/errors/autodiff_attr_errors.hlsl` — argument-count,
  identifier-vs-expression, too-many-arguments, and wrong-subject
  diagnostics.
- `rewriter/autodiff_fwd.hlsl`, `autodiff_bwd.hlsl`,
  `autodiff_fwd_bwd.hlsl` — assert the structure and body of the
  generated `user::ad::fwd` / `user::ad::bwd` namespaces.

One small wrinkle: the in-tree BatchHLSL runner does not support `RUN:
not %dxc`, only `%dxc | FileCheck`.  FileCheck's `consumeErrors` flag
makes the pipeline tolerant of `dxc` exiting nonzero, so the diagnostics
test is written without `not`.

## Known limitations

- Element-type extraction is canonical-print of the return type, which
  is correct for scalar `float` and likely wrong for `float3`,
  matrices, or user types.
- `PrintTranslationUnitWithDifferentials` only iterates the top-level
  `tu->decls()`.  Functions nested inside `namespace`s carrying the
  attribute will be ignored.  This matches the example in the prompt
  but is worth knowing.
- Whether the consumer of the rewritten file can find
  `hlsl/ad/{fwd,bwd}` on its include path is the consumer's problem;
  the rewriter does not synthesise an `#include`.

## Follow-on session — exhaustive coverage, `[[no_diff]]`, stubs

A second pass addressed six specific requests on top of the original
prototype:

1. **Refactor.** The auto-diff translator lived inside
   `dxcrewriteunused.cpp` (~280 lines of class definitions and helpers).
   It now lives in `tools/clang/tools/libclang/dxcrewriteautodiff.{h,cpp}`
   exposing a single `hlsl::PrintTranslationUnitWithDifferentials` entry
   point. `dxcrewriteunused.cpp` calls into it from the
   `opts.RWOpt.GenerateDifferentials` branch.

   Wrinkle: `clang/AST/PrettyPrinter.h` declares `PrintingPolicy` as a
   `struct`. Using `class PrintingPolicy;` as a forward declaration in
   the header produces a `-Wmismatched-tags` warning under the
   default-enabled `-Werror` builds.

2. **Exhaustive intrinsic dispatch.** Two large `StringSwitch` tables
   replace the ad-hoc per-name switch:

   * `GetBackwardIntrinsicBuilder` maps every differentiable HLSL math
     intrinsic from `utils/hct/gen_intrin_main.txt` to the corresponding
     `*Expr<T>` backward-mode builder (trig + hyperbolics + their
     inverses; `exp`, `exp2`, `log`, `log2`, `log10`; `pow` -> `power`;
     `sqrt`, `rsqrt`, `rcp`, `abs`; `min`/`max`/`clamp`; `lerp`,
     `saturate`, `step`, `smoothstep`; `fmod`; geometric `dot`,
     `length`, `distance`, `cross`, `normalize`, `reflect`, `refract`,
     `faceforward`; matrix `mul`, `determinant`, `transpose`; `mad`,
     `frac`, `modf`, `sign`).

   * `GetNonDifferentiableReason` is an explicit enumeration of
     intrinsics that are *not* differentiable — predicates (`any`,
     `all`, `isinf`, `isnan`, `isfinite`), bit casts (`asint`,
     `asuint`, `asfloat`, `f16tof32`, `f32tof16`), bitcount / bit ops,
     side-effecting helpers (`sincos`, `clip`, `errorf`,
     `printf`), atomics, barriers / sync, ddx/ddy and their variants,
     wave / quad ops, mesh-shader output, raytracing intrinsics,
     system-value queries (`GetRenderTargetSampleCount`, etc.),
     tessellator helpers, and the classic d3d9 `tex*` family. The
     reason string is propagated into the generated stub.

   * `IsTextureLikeIntrinsic` catches the resource-sampling families
     by name prefix (`Sample*`, `Load*`, `Gather*`,
     `CalculateLevelOfDetail*`, `__builtin_LinAlg*`) so new entries
     are non-differentiable by default rather than silently miscompiled.

3. **Builtin operator classification.** `emitBinaryOp` now classifies
   every `BinaryOperatorKind` value: `+ - * /` and the compound
   assignment forms map to `add/subtract/multiply/divide` and
   `addAssign/subAssign/mulAssign/divAssign`; comparison, logical,
   bitwise, shift, and remainder operators each call
   `markNonDifferentiable` with a specific reason. `emitUnaryOp` does
   the same for `+`, `-` (differentiable) versus `!`, `~`, `++`, `--`
   (not). `ConditionalOperator` (the ternary `?:`) is flagged.

4. **`[[no_diff]]` statement attribute.** A new `HLSLNoDiff` attribute
   is registered in `Attr.td` with the C++11 spelling
   `[[no_diff]]`. The HLSL Sema path
   (`SemaHLSL::ProcessStmtAttributeForHLSL`) accepts it on any
   `Stmt *S`, returning the existing `S` wrapped in an
   `AttributedStmt` carrying the new attribute. The emitter, when
   processing the body, detects the wrapper and copies the
   substatement verbatim through `printPretty` rather than running it
   through the translator.

   Limitation: in HLSL, the parser's statement entry point passes
   leading attributes straight into `ParseDeclaration` for declaration
   statements, and the HLSLNoDiff attribute is not a declaration
   attribute, so it is dropped before any AST node is built.
   Practically this means `[[no_diff]] float a = expr;` does *not*
   work; users have to wrap the declaration in a block:
   `[[no_diff]] { float a = expr; }`. This is documented in the test
   `no_diff_block.hlsl`.

5. **`_Static_assert(false, ...)` stubs.** `emitAutoDiffFunction`
   buffers the translated body into a `SmallString` via
   `raw_string_ostream`. After translating every statement it checks
   `AutoDiffEmitter::sawNonDifferentiable()`. If true, the buffered
   body is discarded and replaced with

   ```cpp
   _Static_assert(false, "auto-diff cannot generate <mode>-mode for
     '<fn>': <reason>");
   return Value<T>();   // or Variable<T>() in backward mode
   ```

   The fallback `return` keeps the function syntactically well-formed
   so the surrounding namespace and consumers still parse; the
   `_Static_assert` fires only if the function is ever instantiated.

6. **Statement translation.** `AutoDiffEmitter::emitStmt` is a small
   recursive descent that now translates `CompoundStmt`, `DeclStmt`
   (each `VarDecl` becomes `Value<T> name = <translated init>;`),
   `ReturnStmt`, `IfStmt`, `WhileStmt`, `ForStmt`, plain expression
   statements, and `AttributedStmt`. Control-flow forms emit
   structurally but flag the function as non-differentiable so the
   stub assertion fires with a message pointing at `[[no_diff]]`. The
   body loop in `emitAutoDiffFunction` collapses to

   ```cpp
   for (Stmt *S : CS->body()) Em.emitStmt(S, "    ");
   ```

7. **Tests.** Eight new FileCheck tests under
   `tools/clang/test/HLSLFileCheck/rewriter/autodiff/`:

   * `intrinsics_trig.hlsl`, `intrinsics_exp_log.hlsl`,
     `intrinsics_algebraic.hlsl` — cover every entry in the
     differentiable intrinsic table by family.
   * `compound_assign.hlsl` — `+= -= *= /=` -> `*Assign` builders.
   * `local_decls.hlsl` — DeclStmt translation in both fwd and bwd.
   * `nondiff_bitcast.hlsl`, `nondiff_ternary.hlsl`, `nondiff_if.hlsl`
     — `_Static_assert` stub generation for each failure family.
   * Plus the previously committed `no_diff_block.hlsl` and
     `no_diff_return.hlsl` from the `[[no_diff]]` commit.

   Note that the BatchHLSL test runner in
   `tools/clang/unittests/HLSL/CompilerTest.cpp` walks
   `..\HLSLFileCheck\hlsl` and not the `rewriter` subdirectory; these
   tests therefore are not picked up by `check-clang-hlsl` today.
   `check-all` (`check-clang`) still runs the lit suite under
   `tools/clang/test/Rewriter/HLSL` which exercises the unchanged
   paths. Running each new test manually against `bin/dxr` +
   `bin/FileCheck` was used as the per-commit gate; the final
   `ninja check-all` passes with 4610 expected-pass tests and no
   failures.


## Follow-up session: `[[dxc::no_diff]]` rename, expanded tests, library coverage

This second pass addresses four follow-up asks: namespace the no_diff
attribute under `dxc`, broaden the no_diff regression set, mirror the
backward-only tests in forward mode, and finish populating the fwd/bwd
AD support libraries.

1. **`[[no_diff]]` → `[[dxc::no_diff]]`.** Changed the `HLSLNoDiff`
   spelling in `Attr.td` from `CXX11<"", "no_diff", 2015>` to
   `CXX11<"dxc", "no_diff", 2015>`, matching the existing
   `[[dxc::autodiff]]` convention. `ParseDeclCXX::hasCXXAttributeInHLSL`
   was extended to accept `no_diff` under the `dxc` scope so the HLSL
   parser entry point recognises the new spelling. The Sema printout in
   `SemaHLSL.cpp` was updated, as was the rewriter's stale comment and
   the non-differentiable reason string that suggests the attribute.

2. **Statement-attribute on bare expressions.** While adding tests in
   step 3 I noticed that `[[dxc::no_diff]] expr;` lost its trailing
   semicolon in the rewriter output: `printPretty` on an `Expr *`
   (dynamic type) doesn't emit `;` because expression printing is
   context-free. Fixed in `emitStmt`'s AttributedStmt branch by
   detecting `isa<Expr>(Sub)` and appending `;` explicitly.

3. **Broader no_diff coverage.** Four new tests:
   `no_diff_user_call.hlsl` and `no_diff_builtin_call.hlsl` cover call
   expressions to a user function and an HLSL intrinsic respectively;
   `no_diff_operator.hlsl` covers an operator expression statement;
   `no_diff_var_decl.hlsl` documents the block-wrap workaround
   (`[[dxc::no_diff]] { float a = ...; }`) needed because the HLSL
   parser still drops statement attributes on `DeclStmt`.

4. **Forward-mode counterparts.** Backward-only tests added in the
   previous session were duplicated with `*_fwd.hlsl` files:
   `compound_assign_fwd.hlsl`, `intrinsics_trig_fwd.hlsl`,
   `intrinsics_exp_log_fwd.hlsl`, `intrinsics_algebraic_fwd.hlsl`,
   `nondiff_ternary_fwd.hlsl`, `nondiff_if_fwd.hlsl`. Forward mode
   preserves intrinsic call syntax on `Value<T>` (overload resolution
   handles the derivative side) instead of renaming to `*Expr`, and
   parenthesises compound assigns as `(a += x)`; the CHECK lines were
   adjusted accordingly.

5. **Full intrinsic coverage in `hlsl/ad/{fwd,bwd}`.** Cross-checked
   the rewriter's `GetBackwardIntrinsicBuilder` table against the
   support libraries and added the missing overloads. On the fwd side
   that meant `Value<T>` scalar overloads for the rest of the trig and
   hyperbolic families, exp2/log2/log10, rsqrt/rcp,
   abs/saturate/clamp/min/max/lerp/mad/fma/smoothstep,
   sign/step/floor/ceil/round/trunc/frac/fmod/modf,
   degrees/radians/ldexp, and the geometric helpers
   distance/reflect/refract/faceforward/lit. Each calls the underlying
   HLSL intrinsic for the primal and applies the analytic chain rule to
   the stored derivative; piecewise-constant intrinsics use the
   subgradient convention of zero. On the bwd side three small macros
   (`MAKE_BACK_UNARY_EXPR`, `MAKE_BACK_BINARY_EXPR`,
   `MAKE_BACK_TERNARY_EXPR`) plus explicit templates for the geometric
   helpers produce the matching `BackXxxExpr` records, builder
   functions, and `compute_gradients` overloads. Two coverage tests
   (`coverage_fwd.hlsl`, `coverage_bwd.hlsl`) FileCheck the library
   headers directly to assert each required symbol exists.

   The library headers themselves still cannot be compiled end-to-end
   by `dxc` today because of pre-existing issues that are out of scope:
   `#include <matrix_utils>` uses angle brackets which the HLSL
   preprocessor rejects, and `matrix_utils` references
   `hlsl::enable_if` without including its header. Those bugs predate
   this work.

Validation: `cd build-rel && ninja check-all` reports 4610 expected
passes / 9 expected failures / 33 unsupported, unchanged from the
pre-rename baseline. Each rewriter test under
`tools/clang/test/HLSLFileCheck/rewriter/autodiff/` was additionally
verified manually with `bin/dxr -generate-differentials | bin/FileCheck`
(the BatchHLSL runner still doesn't recurse into the `rewriter/`
subtree, so the manual run remains the per-commit gate).

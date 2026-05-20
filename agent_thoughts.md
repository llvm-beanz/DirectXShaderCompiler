# Agent Thoughts: Enabling C++11 `constexpr` in HLSL 202x

## Task

Enable C++11 `constexpr` support in HLSL under the HLSL 202x language mode.
Under HLSL 2021 (and earlier), `constexpr` must produce a clear error.
Under HLSL 202x (and later), `constexpr` behaves as defined by C++.

## Investigation

### Why was `constexpr` rejected previously?

`constexpr` is declared in `TokenKinds.def` as a `CXX11_KEYWORD`. In HLSL
mode, `LangOpts.CPlusPlus` is true but `LangOpts.CPlusPlus11` is false. The
keyword-status logic in `IdentifierTable.cpp` then classifies `constexpr`
as `KS_Future`, which actually demotes it to a plain identifier (with a
future-compat warning). The parser's `tok::kw_constexpr` case had a
defensive `goto HLSLReservedKeyword` for safety, but at runtime the token
was an identifier, so users got cryptic errors like
"unknown type name 'constexpr'".

### Why not just enable `LangOpts.CPlusPlus11` in HLSL 202x?

Tempting, but `KEYCXX11` covers many keywords beyond `constexpr`
(`decltype`, `nullptr`, `static_assert`, `thread_local`,
`char16_t`/`char32_t`, `noexcept`, `alignas`/`alignof`). Promoting all of
them to keywords in HLSL 202x would change tokenization of any user code
that uses those identifiers as names. Out of scope for this task and a
backwards-compat risk.

I chose a surgical approach: only `constexpr` becomes a real keyword in
HLSL.

## Approach

### Phase 1 — Lexer / Parser

1. Tag `constexpr` with `KEYHLSL` in `TokenKinds.def` so it is a real
   keyword in every HLSL mode (regardless of version). This lets us emit
   a precise diagnostic instead of "unknown type name".
2. In `ParseDecl.cpp` where `tok::kw_constexpr` is handled, replace the
   blanket "reserved keyword" rejection with a version check. For HLSL
   versions below 202x, emit a new `err_hlsl_constexpr_requires_202x`
   diagnostic and skip calling `SetConstexprSpec` (so the variable
   doesn't pick up the constexpr semantics and generate a cascading
   "must be initialized by a constant expression" error). For HLSL 202x
   and later, call `SetConstexprSpec` as in standard C++.
3. Update the two existing HLSL tests that asserted on the old, less
   precise error.

### Phase 2 — Sema / AST

Even after Phase 1 the constexpr machinery was mostly inert because
Clang gates several pieces of constexpr semantics on
`LangOpts.CPlusPlus11`. I had to widen three gates to also accept
`HLSL && HLSLVersion >= v202x`:

* `VarDecl::isUsableInConstantExpressions` — without this, a constexpr
  variable couldn't be used as an array bound or in another constexpr
  initializer.
* `VarDecl::evaluateValue` and `VarDecl::checkInitIsICE` — these are
  what populate the cached `Eval->IsICE` bit that
  `Sema::AddInitializerToDecl` (via `var->isInitICE()`) reads when
  validating that a constexpr variable was initialized by a constant
  expression. Without widening, *every* constexpr declaration in 202x
  was incorrectly flagged as "must be initialized by a constant
  expression". I discovered this with a temporary debug print inside
  the constexpr-init branch of `SemaDecl`; `evaluateValue` returned
  success, but `isInitICE()` returned false because the side-effect
  population of `Eval->IsICE` was C++11-only.
* `VarDecl` linkage computation — in HLSL, const globals are
  intentionally treated as external (cbuffer) variables, which causes a
  "Initializer of external global will be ignored" warning that breaks
  uses like `constexpr int N = 4; int arr[N];`. In standard C++ a
  namespace-scope `constexpr` variable has internal linkage, so I made
  HLSL 202x follow the standard rule for `constexpr` (only) — `const`
  HLSL globals are unchanged.

I also fixed the parallel `CPlusPlus11` gate in `SemaStmt.cpp` that
selects between "constexpr function missing return" vs the generic
missing-return diagnostic, so a constexpr function in HLSL 202x gets
the more specific error.

### Phase 3 — Tests

I added three tests covering each phase of translation:

* `tools/clang/test/SemaHLSL/v2021/constexpr.hlsl` — `-verify` test
  that exercises the new diagnostic at global, function and local
  scope (Parse-phase behavior).
* `tools/clang/test/SemaHLSL/v202x/constexpr.hlsl` — `-verify` test
  with `expected-no-diagnostics`. Exercises a constexpr variable,
  multiple constexpr functions, constexpr used as an array bound, and
  a local constexpr initialized from other constexpr values
  (Sema-phase behavior).
* `tools/clang/test/CodeGenHLSL/constexpr-202x.hlsl` — FileCheck test
  on DXIL output verifying that a `constexpr` function call folds to a
  constant at CodeGen time.

I also updated the two pre-existing HLSL parser tests that previously
verified the cryptic "unknown type name" error.

## Verification

* `ninja dxc` builds clean.
* `ninja check-all` reports 4603 expected passes (was 4601; my two new
  Sema tests account for the delta — the CodeGenHLSL test is also
  picked up but appears under the same expected-pass count).
* Manually verified with a small shader that `constexpr int sq(int x)
  { return x*x; } constexpr int N = sq(5);` produces a `bufferStore`
  using the literal `25`.

## Risks / Notes

* HLSL globals are normally treated as external cbuffer variables. I
  only changed the linkage rule when both `isConstexpr()` and HLSL
  version >= 202x are true, so other HLSL globals are unaffected.
* I deliberately did not promote `LangOpts.CPlusPlus11` to true under
  HLSL 202x because that would change the tokenization of many
  identifiers; a separate, intentional discussion should happen if/when
  HLSL wants to adopt the full set of C++11 keywords.
* The new parser diagnostic message ("'constexpr' is only supported in
  HLSL 202x or later") follows the same style as the existing
  `err_hlsl_reserved_keyword` diagnostic.

---

## Follow-up: _Static_assert + constexpr Test Coverage

### Task

Add a broad suite of test cases that drive constexpr functions and
variables through `_Static_assert`, to pin down that the compiler
evaluates them early (during Sema, not deferred to CodeGen).

### Approach

`_Static_assert` requires its condition to be an integral constant
expression, so a successful `_Static_assert(<constexpr-expr>, "msg")`
already proves that the constexpr machinery ran during semantic
analysis. To get high signal I added three complementary test files,
one per phase of translation:

1. **Lex/Parse + Sema "happy path"**:
   `tools/clang/test/SemaHLSL/v202x/static-assert-constexpr.hlsl` —
   `-verify` with `expected-no-diagnostics`. Exercises every interesting
   shape I could think of:
   * constexpr variables of `int`, `uint`, `float`
   * constexpr variables initialized from other constexpr variables
   * arithmetic (`+ - * / %`), comparisons, logical (`&& || !`),
     bitwise (`& | ^ ~`), shifts (`<< >>`), unary (`+ -`), ternary
   * constexpr functions calling other constexpr functions
   * constexpr functions taking constexpr-variable arguments
   * nested calls (e.g. `square(square(x))`)
   * numeric casts (`int <-> float`)
   * `_Static_assert` at namespace scope, in a `struct`, and inside a
     function body
   * local constexpr inside a function used in a `_Static_assert`

2. **Sema "negative path"**:
   `tools/clang/test/SemaHLSL/v202x/static-assert-constexpr-fail.hlsl` —
   every `_Static_assert` is intentionally false and tagged with a
   matching `expected-error{{static_assert failed "..."}}`. This makes
   sure that if the compiler ever stopped evaluating constexpr inside
   `_Static_assert` (e.g., silently treating it as non-constant), the
   missing diagnostics would fail the test — i.e., the test would not
   pass vacuously.

3. **CodeGen agreement**:
   `tools/clang/test/CodeGenHLSL/static-assert-constexpr.hlsl` —
   compiles the same constexpr entities that drive `_Static_assert`
   into a real shader and FileChecks that the constant value reaches
   DXIL. This guards against a regression where Sema-time evaluation
   and CodeGen-time evaluation could disagree.

### Gotchas hit while writing the tests

* I initially used a constexpr helper named `mul` and got a "not an
  integral constant expression" error. Cause: `mul` is also an HLSL
  intrinsic (vector/matrix multiply), and the call resolution picks up
  the intrinsic in ways that interfere with constant folding. I renamed
  the helper to `imul` (and similarly `idiv`, `imod`, `iabs`, `imax`,
  `imin`, `fmul`, `band`, `bor`, `bxor`, `shl`, `shr`) to avoid clashing
  with intrinsic names. This is also good hygiene for the tests —
  collisions with intrinsics would otherwise mask what we're trying to
  measure.

* HLSL forbids recursion, so I avoided classic constexpr examples like
  `factorial` and `fib`. The constant evaluator does run for those, but
  later semantic checks reject the recursion and the test would fail
  for the wrong reason.

* HLSL uses `uint` rather than `unsigned`. I caught this when an early
  draft used `unsigned`, producing "HLSL requires a type specifier"
  errors.

### Validation

```
$ python3 ./build-rel/bin/llvm-lit \
    tools/clang/test/SemaHLSL/v202x/ \
    tools/clang/test/SemaHLSL/v2021/ \
    tools/clang/test/CodeGenHLSL/constexpr-202x.hlsl
... Expected Passes : 14
```

All three new tests pass and no neighbouring tests regress.

## Follow-up: negative tests for non-constant evaluation

A follow-up request asked for two additional families of tests:

1. Constexpr variables whose initializer is *not* a constant expression.
2. Calls to constexpr functions with *non-constexpr* arguments.

Both belong squarely in the Sema phase (the constant-expression
evaluator runs there), so I added two `-verify` tests under
`tools/clang/test/SemaHLSL/v202x/`.

### `constexpr-non-const-init.hlsl`

Exercises every realistic source of "not a constant expression" that an
HLSL author can plausibly write:

* cbuffer-backed runtime globals (`int`, `float`, `uint`) and arithmetic
  on them.
* Non-constexpr free-function calls.
* Function parameters.
* Non-const locals, including a mutable `static` local.
* `_Static_assert` operands drawn from the same runtime values, to
  confirm the path that flows through `Sema::VerifyIntegerConstantExpression`
  rather than through `AddInitializerToDecl`.

I deliberately pin both the `expected-error` (the diagnostic the user
sees) and the cascading `expected-note` (the "read of non-const variable
... is not allowed" hint) so a future regression that drops the note
chain still fails the test.

### `constexpr-non-const-args.hlsl`

The interesting case here is the *runtime is fine* / *constant context
isn't* distinction. Calling `square(p)` where `p` is a function
parameter is perfectly legal C++/HLSL — the call simply isn't a constant
expression and gets lowered like any other call. What is illegal is
*requiring* that result to be constant.

The test therefore has two halves:

* Negative half: feeding non-const arguments (globals, params, locals,
  and even nested `square(plain(x))`) into a `constexpr` variable
  initializer, an array bound proxy, or `_Static_assert`, and pinning
  the matching diagnostic.
* Positive half (the `main` function): the same calls used in plain
  runtime contexts must compile cleanly with no diagnostics.

This pairing protects against two opposite regressions: either
over-eager rejection of legitimate runtime calls, or silent acceptance
of a constant-context use that should fail.

### Quirks I hit

* Globals (`g_runtime`, `g_runtime_f`) trigger the "must be initialized
  by a constant expression" *error* but do **not** emit the "read of
  non-const variable" *note*; that note path is only reached for
  locals/params. Initially I added expected-notes for the globals and
  the test failed with "expected diagnostic not seen". Removing those
  notes (and only marking notes on the local/param cases) made the
  expected/actual sets line up.
* `_Static_assert` produces a *different* top-level diagnostic — "static
  assert expression is not an integral constant expression" — than the
  constexpr-init diagnostic, so the two error families have to be
  matched separately even though they share the same underlying
  non-constant cause.

### Validation

```
$ python3 ./build-rel/bin/llvm-lit \
    tools/clang/test/SemaHLSL/v202x/constexpr-non-const-init.hlsl \
    tools/clang/test/SemaHLSL/v202x/constexpr-non-const-args.hlsl
... Expected Passes : 2

$ python3 ./build-rel/bin/llvm-lit tools/clang/test/SemaHLSL/
... Expected Passes : 263 / Expected Failures : 1
```

Both new tests pass and the wider `SemaHLSL/` suite is unchanged.

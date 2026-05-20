# Agent thoughts: removing the implicit `using namespace hlsl;` under 202x

## Problem statement

`tools/clang/lib/Sema/SemaHLSL.cpp` was previously updated to enable the
implicit `hlsl` namespace under HLSL 202x and to inject an implicit
`using namespace hlsl;` directive at translation-unit scope so that
unqualified HLSL intrinsic calls (`sin`, `cos`, `dot`, …) continued to
resolve transparently. The user asked for the *opposite* end state: keep
the namespace, but remove the implicit using-directive and make HLSL
202x require the `hlsl::` qualifier for HLSL built-in intrinsics.
Unqualified intrinsic references must fail; qualified ones must succeed.

## Investigation

Three pieces of machinery cooperate to resolve HLSL intrinsic calls
inside `HLSLExternalSource`:

1. `Initialize()` creates the implicit `hlsl` namespace under 202x and
   (before this change) injected a `UsingDirectiveDecl` nominating it
   into the translation-unit scope.
2. `AddOverloadedCallCandidates()` is invoked from
   `Sema::AddOverloadedCallCandidates()` once an `UnresolvedLookupExpr`
   has been built. It inspects the qualifier on the ULE and, for
   "interesting" qualifiers, searches the corresponding intrinsic table
   (`g_Intrinsics` for HLSL, `g_DxIntrinsics` for `dx::`,
   `g_VkIntrinsics` for `vk::`), creating the matching `FunctionDecl`
   on demand and adding it to the candidate set.
3. `Sema::UseArgumentDependentLookup()` decides whether ADL is enabled
   for the current call. The HLSL fork allows ADL even when a scope
   specifier is present, but only for the `vk::` and `dx::` namespaces,
   so qualified calls into those namespaces still reach the external
   source's `AddOverloadedCallCandidates` hook.

For `sin(x)` to resolve under the old (using-directive) implementation,
unqualified name lookup returns no decls but ADL is left enabled, so a
ULE with `requiresADL=true` is built and the HLSL hook materializes the
intrinsic. The implicit using-directive was not actually required for
the unqualified path to work: ADL on the unqualified ULE is what drives
intrinsic materialization. The directive only mattered for follow-up
qualified-lookup `hlsl::sin` references, where the namespace had to be
populated by a prior unqualified call.

For the new behavior the chain needs to change in two places:

* The ADL-driven path for unqualified calls must be turned off under
  202x. Without that, removing the using-directive alone does *not*
  make `sin(x)` fail — ADL would still synthesize the candidate.
* The qualified `hlsl::sin(x)` path must reach the HLSL hook without
  any prior unqualified materialization. That requires extending
  `UseArgumentDependentLookup()` so an `hlsl::`-qualified call enables
  ADL (just like `vk::` and `dx::` do today).

## Design

I split the work into focused commits:

1. **`SemaHLSL.cpp` / `SemaExpr.cpp`.**
   * Remove the `UsingDirectiveDecl` block from
     `HLSLExternalSource::Initialize`.
   * In `AddOverloadedCallCandidates`, only add `g_Intrinsics` to the
     `SearchTables` for unqualified calls when the language version is
     older than HLSL 202x. Qualified entry points (`::name`,
     `hlsl::name`, `dx::name`, `vk::name`) keep their existing
     behavior.
   * Replace the `assert(!SearchTables.empty(), ...)` with a clean
     "return false" so that an unqualified 202x call with no table to
     search falls back to Clang's default overload resolution — the
     candidate set ends up empty and `BuildRecoveryCallExpr` emits the
     standard `use of undeclared identifier` diagnostic.
   * In `Sema::UseArgumentDependentLookup`, treat the implicit `hlsl`
     namespace the same way `vk` and `dx` are treated, so that
     `hlsl::sin(x)` enables ADL and the HLSL hook gets called. The
     check guards on `NamespaceDecl::isImplicit()` so a user-defined
     `namespace hlsl { ... }` does not start receiving the special
     ADL treatment.

2. **Existing 202x tests.** Several lit tests were already exercising
   unqualified HLSL intrinsic calls under `-HV 202x`:
   * `CodeGenDXIL/hlsl/linalg/builtins/{matrixstoretomemory,
     matrixaccumulatetomemory,matrixloadfrommemory,convert,
     vectoraccumulatetodescriptor}/nominal.hlsl` — qualified to
     `hlsl::__builtin_LinAlg_*`.
   * `SemaHLSL/hlsl/linalg/builtins/{matrixaccumulatetomemory,
     matrixloadfrommemory,matrixstoretomemory}/unavailable_pre_sm610.hlsl`
     — qualified to `hlsl::__builtin_LinAlg_*`. (The expected diagnostic
     text already printed the `hlsl::` prefix.)
   * `SemaHLSL/v202x/conforming-literals/asfloat16.hlsl` — qualified to
     `hlsl::asfloat16` so the `no matching function` diagnostic still
     fires instead of `use of undeclared identifier`.

3. **Namespace-specific tests.**
   * `SemaHLSL/hlsl-namespace-202x.hlsl` — rewritten to drive intrinsic
     materialization through `hlsl::sin`/`hlsl::cos`/`hlsl::dot` and to
     assert that no `UsingDirectiveDecl` nominates the `hlsl`
     namespace.
   * `SemaHLSL/hlsl-namespace-qualified-intrinsics.hlsl` — rewritten as
     a `-verify` test that asserts qualified calls compile and
     unqualified `sin`/`dot` produce
     `use of undeclared identifier` diagnostics.
   * `CodeGenDXIL/hlsl/intrinsics/hlsl-namespace-qualified-call.hlsl` —
     rewritten so two `hlsl::sin` calls (with no prior unqualified use)
     both lower to `dx.op.unary` Sin opcode 13.
   * `SemaHLSL/hlsl-namespace-no-implicit-using-202x.hlsl` — new
     focused `-verify` test that walks through four common intrinsics
     (`sin`, `cos`, `dot`, `saturate`) and asserts that each diagnoses
     `use of undeclared identifier` unqualified while its
     `hlsl::`-qualified form compiles.
   * `SemaHLSL/hlsl-namespace-pre-202x.hlsl` and
     `SemaHLSL/hlsl-namespace-not-available-pre-202x.hlsl` were left
     untouched — pre-202x behavior is unchanged.

## Tests covering each phase of translation

* **Parser / Sema name lookup.** The `-verify` tests
  `hlsl-namespace-qualified-intrinsics.hlsl`,
  `hlsl-namespace-no-implicit-using-202x.hlsl`, the pre-202x
  availability tests in `linalg/builtins/.../unavailable_pre_sm610.hlsl`
  and the updated `asfloat16.hlsl` test exercise the Sema-level
  qualified/unqualified resolution rules and the diagnostics emitted
  when an unqualified intrinsic is used.
* **AST shape.** `hlsl-namespace-202x.hlsl` (FileCheck on
  `-ast-dump-implicit`) verifies that the implicit `hlsl` NamespaceDecl
  exists, that intrinsic FunctionDecls are parented to it, and that no
  implicit `UsingDirectiveDecl` nominates it. The companion
  `hlsl-namespace-pre-202x.hlsl` still verifies that no such namespace
  exists pre-202x.
* **DXIL code generation.**
  `CodeGenDXIL/hlsl/intrinsics/hlsl-namespace-qualified-call.hlsl`
  confirms that a `hlsl::`-qualified intrinsic call lowers to the
  expected `dx.op.unary` Sin op without requiring any earlier
  unqualified use, and the updated linalg `nominal.hlsl` tests confirm
  the linAlg intrinsic chain still lowers correctly through the
  qualified entry points.

## Verification

* `ninja check-all` — 4610 expected passes, 9 expected failures (pre-
  existing), 33 unsupported, 0 unexpected failures.
* `ninja check-clang-unit` — 839/839 passes.

## Commits

1. `[SemaHLSL] Require 'hlsl::' qualifier for HLSL intrinsics under
   HLSL 202x` — the SemaHLSL.cpp + SemaExpr.cpp change.
2. `[Test][HLSL] Use 'hlsl::' qualifier for HLSL intrinsics under
   202x` — preexisting 202x lit tests updated to use the new qualified
   form.
3. `[Test][SemaHLSL] Cover the 'hlsl::' qualifier requirement under
   202x` — the namespace-specific tests rewritten/added.
4. This `agent_thoughts.md` update (committed by itself, as
   instructed).

---

# Agent thoughts: explicit `using namespace hlsl;` at different scopes

## Problem statement

The prior change made HLSL 202x require the `hlsl::` qualifier for any
HLSL built-in intrinsic.  The follow-up request was to allow programs
that explicitly write `using namespace hlsl;` to call HLSL intrinsics
unqualified — exactly the way `using namespace dx;` already lets users
call DirectX intrinsics unqualified — and to add tests verifying this at
several different scopes.

## Investigation

`HLSLExternalSource::AddOverloadedCallCandidates` already collected the
set of namespace contexts reachable from the current `Scope*` via
`Sema::CollectNamespaceContexts` and toggled `SearchDX`/`SearchVK` when
it found the implicit `dx`/`vk` namespace among them.  No analogous
toggle existed for the implicit `hlsl` namespace; when a program wrote
`using namespace hlsl;` at any scope under 202x and then called `sin(x)`
unqualified, the hook returned without ever searching `g_Intrinsics`,
and Clang emitted `use of undeclared identifier 'sin'`.

I verified this by hand-running `dxc -T lib_6_3 -HV 202x` against a tiny
fixture that used `using namespace hlsl;` at translation-unit scope —
that initial run reproduced the diagnostic.

## Design

The fix is symmetric with the existing dx/vk handling:

* Add a `SearchHlslViaUsing` flag that becomes true when the
  `CollectNamespaceContexts` walk discovers `m_hlslNSDecl` (i.e. an
  in-scope `using namespace hlsl;`).
* Reorder the local block in `AddOverloadedCallCandidates` so the
  using-directive scan runs before the decision of whether to push
  `g_Intrinsics` into `SearchTables`, since that decision now depends
  on the scan result.
* Adjust `searchHlslIntrinsics`: under 202x the HLSL intrinsic table
  is searched for unqualified calls when either the program is
  pre-202x (existing behavior) **or** an in-scope using-directive
  nominates the implicit `hlsl` namespace.
* The qualified path (`hlsl::name`, `::name`) is unchanged and
  continues to work whether or not a using-directive is present.

`Sema::UseArgumentDependentLookup` did not need further changes: for
unqualified calls ADL is already enabled, so the call expression
becomes an `UnresolvedLookupExpr` and the HLSL hook still gets to run.

## Tests

Sema (`-verify`) tests are organized by the scope at which the
using-directive sits:

* `SemaHLSL/hlsl-using-namespace-tu-scope-202x.hlsl` —
  `using namespace hlsl;` at translation-unit scope; multiple
  intrinsics (`sin`, `cos`, `dot`, `saturate`) all resolve unqualified
  and the qualified form is also accepted.
* `SemaHLSL/hlsl-using-namespace-function-scope-202x.hlsl` — the
  using-directive is placed in a function body and in a nested compound
  statement; sibling functions and code outside the inner block still
  emit `use of undeclared identifier` so we know the directive is
  scoped correctly.
* `SemaHLSL/hlsl-using-namespace-namespace-scope-202x.hlsl` — the
  directive is placed inside a user-defined namespace; intrinsic
  references inside that namespace (including from out-of-line member
  definitions) resolve, while an unrelated sibling namespace and TU
  scope still require the qualifier.

A CodeGen test covers the later phases of translation:

* `CodeGenDXIL/hlsl/intrinsics/hlsl-using-namespace-call.hlsl` — a
  translation-unit `using namespace hlsl;` followed by an unqualified
  `sin` call; the test asserts that the resulting DXIL contains the
  expected `dx.op.unary` `Sin` op (opcode 13).

## Verification

* `ninja check-clang` — 2986 expected passes, 7 expected failures
  (pre-existing), 9 unsupported, 0 unexpected failures.
* All three new SemaHLSL tests and the new CodeGenDXIL test pass.
* The previously-added namespace tests
  (`hlsl-namespace-no-implicit-using-202x.hlsl`,
  `hlsl-namespace-qualified-intrinsics.hlsl`, etc.) still pass,
  confirming that programs which do *not* nominate the namespace still
  see the diagnostic.

## Commits

1. `[SemaHLSL] Honor explicit 'using namespace hlsl;' under HLSL 202x`
   — the SemaHLSL.cpp change.
2. `[Test][HLSL] Cover explicit 'using namespace hlsl;' at various
   scopes` — the four new lit tests (three Sema, one CodeGen).
3. This `agent_thoughts.md` update, committed by itself.

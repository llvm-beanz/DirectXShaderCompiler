# Agent thoughts: enabling the `hlsl` namespace under HLSL 202x

## Problem statement

`tools/clang/lib/Sema/SemaHLSL.cpp` historically created an implicit `hlsl`
namespace in `HLSLExternalSource::Initialize`, but the creation was gated
behind a `false &&` guard pending the resolution of
<https://github.com/microsoft/hlsl-specs/issues/484>. The linked spec issue
proposes that, for HLSL 202x, DXC should mirror Clang's HLSL implementation:
place HLSL built-in types and intrinsics into a real `hlsl` namespace and
automatically `using namespace hlsl;` so existing unqualified user code keeps
working. The user asked us to enable the guarded line under 202x and add
tests covering namespace placement and resolution.

## Investigation

* The `if (false && ...)` block in `Initialize` (around line 5595) created
  the `NamespaceDecl` for `hlsl` but never registered it with the translation
  unit. Even if the guard were flipped, the namespace would not be findable
  by qualified lookup because it was never added as a child of the TU and
  did not advertise any external storage to Sema.
* The `dx` namespace (and the `vk` namespace under SPIR-V) is the right
  template for what the `hlsl` namespace needs: it is `setImplicit`,
  `setHasExternalLexicalStorage(true)`, and added to
  `context.getTranslationUnitDecl()` in `InitializeSema`. Intrinsics in the
  table are eagerly pushed into the namespace via
  `AddIntrinsicFunctionsToNamespace`.
* The intrinsic-lookup code in `AddOverloadedCallCandidates` already
  honored the value of `m_hlslNSDecl` and parented each newly-created
  HLSL intrinsic `FunctionDecl` to it (see line 5485 in the original code),
  but only if that pointer was non-null. So enabling the guard is enough to
  start placing intrinsics in the namespace, provided the namespace is
  visible to name lookup.
* Qualified lookup of `hlsl::sin` would still fail until the intrinsic was
  materialized in the namespace. Intrinsics are materialized lazily by
  `AddOverloadedCallCandidates`, which itself is reached via an
  `UnresolvedLookupExpr` (`ULE`). The `ULE` is only formed when standard
  name lookup succeeds, so the first reference must be unqualified (or
  global). For the issue's primary stated goal — DXC behaving like Clang
  with respect to collisions between user globals and intrinsics — placing
  intrinsics into the namespace plus the implicit using-directive is
  sufficient.

## Design

I broke the work into focused, separately reviewable steps:

1. **Enable the namespace under 202x and register it on the translation
   unit.** This is the literal answer to "enable that line under HLSL 202x".
   The namespace is created with `setImplicit`,
   `setHasExternalLexicalStorage(true)`, and added to the TU just like the
   `dx` namespace. Once it exists, every intrinsic that
   `AddOverloadedCallCandidates` synthesizes lands inside it (because the
   existing code already used `m_hlslNSDecl` as the parent).
2. **Inject an implicit `using namespace hlsl;` at TU scope under 202x.**
   Without this, existing HLSL code that calls `sin(x)` unqualified would
   stop compiling, because the intrinsic would no longer be a direct child
   of the TU. The using-directive is added during `InitializeSema` (so that
   `TUScope` is available) by creating a `UsingDirectiveDecl`, marking it
   implicit, adding it as a child of the TU, and pushing it onto the TU
   scope through `Sema::PushUsingDirective`.
3. **Teach `AddOverloadedCallCandidates` about the `hlsl::` qualifier.** The
   existing logic short-circuited any qualified call that wasn't `::`,
   `dx::`, or `vk::`. I extended that allow-list to include `hlsl::` and
   added `hlsl::` to the set of qualifiers that triggers an HLSL intrinsic
   table search. This makes follow-up qualified calls (after an intrinsic
   is already in the namespace) resolve through the HLSL overload
   resolution path rather than falling back to the generic Clang path.
4. **Update existing tests whose diagnostics now print the `hlsl::`
   prefix.** Three SemaHLSL availability tests in `linalg/builtins/...`
   asserted on the exact diagnostic text "intrinsic
   __builtin_LinAlg_...". Since the intrinsic now lives in `hlsl::`, the
   pretty printer emits `hlsl::__builtin_LinAlg_...`, so I updated those
   assertions.
5. **Add new tests.**

## Tests added

* `tools/clang/test/SemaHLSL/hlsl-namespace-202x.hlsl` — `-ast-dump-implicit`
  shows the implicit `NamespaceDecl hlsl`, that `sin`, `cos`, and `dot` are
  parented to it, and that an implicit `UsingDirectiveDecl` targets the
  namespace.
* `tools/clang/test/SemaHLSL/hlsl-namespace-pre-202x.hlsl` — same dump
  under `-HV 2021` confirms neither the namespace nor the using-directive
  is created, and the intrinsic stays at TU scope.
* `tools/clang/test/SemaHLSL/hlsl-namespace-qualified-intrinsics.hlsl` —
  `-verify` run that exercises `hlsl::sin` and `hlsl::dot` alongside their
  unqualified spellings and asserts no diagnostics.
* `tools/clang/test/SemaHLSL/hlsl-namespace-not-available-pre-202x.hlsl` —
  `-verify` run that uses `hlsl::sin` under `-HV 2021` and asserts the
  expected "use of undeclared identifier 'hlsl'" diagnostic.
* `tools/clang/test/CodeGenDXIL/hlsl/intrinsics/hlsl-namespace-qualified-call.hlsl`
  — codegen test confirming that both unqualified and `hlsl::`-qualified
  intrinsic calls lower to the same DXIL `dx.op.unary` Sin opcode.

## Scope notes / future work

* **Type templates and object types remain in the translation unit.**
  Templates such as `vector` and `matrix`, and object types such as
  `Texture2D`, `RWBuffer`, etc., are still created as children of the
  translation unit declaration in `ASTContextHLSL.cpp` and
  `AddObjectTypes`. Under 202x they are still accessible unqualified
  because of the implicit using-directive, but `hlsl::vector` and
  `hlsl::Texture2D` are not yet directly resolvable, because those decls
  are not actually members of the `hlsl` namespace. Moving them is a
  larger refactor that touches a long list of `BuiltinTypeDeclBuilder`
  call sites, every place that asks for a type's `DeclContext`, the
  `m_objectTypeDecls` machinery, the linalg builtins, and several SPIR-V
  paths. It is out of scope for the change requested here ("enable that
  line"), but is the natural next step toward matching Clang's HLSL
  namespacing fully.
* **Lazy qualified lookup of intrinsics.** Qualified `hlsl::name` only
  works after `name` has already been materialized in the namespace by
  an unqualified reference elsewhere in the same translation unit. A
  cleaner end state would either (a) eagerly add all intrinsic templates
  to the `hlsl` namespace at namespace-creation time (the way `dx`
  intrinsics are handled), or (b) override
  `ExternalASTSource::FindExternalVisibleDeclsByName` so qualified
  lookups can materialize an intrinsic on first reference. Either change
  is a logical follow-up.

## Verification

* `ninja check-all` (lit suite) passes: 4609 expected passes, no
  unexpected failures.
* `ninja check-clang-semahlsl` and `ninja check-clang-codegendxil-hlsl-intrinsics`
  pass.
* Manual smoke tests with `dxc -T lib_6_6 -HV 202x` confirm:
  * `-ast-dump-implicit` shows `sin`, `cos`, `dot` inside `namespace hlsl`.
  * Unqualified `sin(x)` and qualified `hlsl::sin(x)` both compile.
  * Under `-HV 2021`, `hlsl::sin` errors with "use of undeclared
    identifier 'hlsl'".

## Commits

Changes were broken into the smallest reasonable pieces, with the source
change separated from the new test files:

1. `[SemaHLSL] Enable hlsl namespace and implicit using-directive under
   HLSL 202x` — the SemaHLSL.cpp change plus the three test-expectation
   updates that depend on it.
2. `[Test][SemaHLSL] Add tests for the HLSL 202x 'hlsl' namespace` — the
   five new lit test files described above.
3. This `agent_thoughts.md` (committed by itself, as instructed).

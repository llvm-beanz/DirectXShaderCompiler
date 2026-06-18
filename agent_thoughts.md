# Agent thoughts

Working notes captured while implementing the four features requested in
`agent_prompt.md`. Each entry corresponds to one commit landed on the
`agent/traits_and_casts` branch.

## 1. `__is_intangible` and `__is_scalar_layout_compatible`

These two traits share enough plumbing (parser allow-listing, keyword
registration, completeness checks) that I implemented them in a single
commit.

* Both traits are registered in `TokenKinds.def` with `KEYHLSL` so they
  become keyword tokens only in HLSL mode and remain ordinary identifiers
  elsewhere.
* `ParseTypeTrait` in `ParseExprCXX.cpp` previously rejected *all* type
  traits in HLSL with `err_hlsl_unsupported_construct`. I narrowed that
  rejection to be an allow-list, letting only our two new traits through;
  the standard `__is_pod` family remains diagnosed as unsupported as
  before.
* `__is_intangible` is implemented as `hlsl::IsHLSLIntangibleType`. The
  function strips qualifiers and arrays, then returns true for: HLSL
  resource types (`HLSLResourceAttr`), node objects, node record arrays,
  hit objects, ray queries, dynamic resources/samplers, and any
  user-defined record whose bases or fields transitively contain such a
  type. Vector and matrix types short-circuit to `false` since their
  element types must already be numeric. The trait is registered as a
  completeness-required UTT so we can inspect record fields.
* `__is_scalar_layout_compatible` flattens both operands into a vector
  of scalar leaf `QualType`s (via the new
  `hlsl::GetHLSLFlattenedScalarTypes`) and then checks that the two
  flattenings have equal non-zero length and that each pair is either
  the same type or both arithmetic. Intangible inputs are rejected
  outright.

## 2. `__builtin_bit_cast`

* Registered as a `KEYHLSL` keyword. Because
  `ParseBuiltinPrimaryExpression` explicitly asserts `!HLSL`, I parse
  `__builtin_bit_cast(T, expr)` inline inside `ParseCastExpression`.
* `Sema::ActOnHLSLBuiltinBitCast` validates that the sizes match, that
  neither side is intangible, and reuses the existing `AsTypeExpr` AST
  node. This avoids inventing a new AST class purely for HLSL while
  still letting the existing scalar/vector CodeGen path emit the LLVM
  `bitcast` instruction.
* For the first cut I restricted the operand and result types to
  scalars and HLSL vectors. Aggregates (records, matrices, arrays) emit
  a clear diagnostic so the door is left open for a follow-up that
  teaches `CGExprAgg` how to lower an aggregate `AsTypeExpr` (likely as
  alloca + pointer bitcast + load).
* New diagnostics live alongside the other HLSL ones:
  `err_hlsl_bit_cast_size_mismatch`, `err_hlsl_bit_cast_intangible`,
  and `err_hlsl_bit_cast_unsupported_type`.

## 3. `<bit_cast>` header

* Added `tools/clang/lib/Headers/hlsl/bit_cast.h` and listed it in
  `tools/clang/lib/Lex/CMakeLists.txt`. The embed-headers build step
  picks it up automatically and exposes it via the in-binary header
  table — no filesystem search needed.
* The header defines `template <typename To, typename From> To
  hlsl::bit_cast(From value)` whose body is just `return
  __builtin_bit_cast(To, value);`. All checking happens in the builtin.
* Discovered that `TreeTransform::TransformAsTypeExpr` was a stub that
  asserted unreachable. That stub fired immediately when the template
  was instantiated, aborting the compiler. I implemented the
  transformer to recurse on the source expression and rebuild via
  `ActOnHLSLBuiltinBitCast` so the size/intangible checks rerun against
  the substituted types. This is also why the failing-case Sema test
  pins the diagnostic to a line inside the header.

## Validation

After each commit I ran the regression suite using the prescribed
`PredefinedParams.cmake` cache. The final state of `ninja check-all`:

```
Expected Passes    : 4630
Expected Failures  : 9
Unsupported Tests  : 33
```

No new failures were introduced.

## Follow-up: unifying the type predicates and reusing FlattenedTypeIterator

A code review pointed out two duplications worth fixing as small NFC
commits.

### `IsHLSLIntangibleType` vs `IsHLSLNumericOrAggregateOfNumericType`

These are the two halves of the same partition: a type either has a
front-end-known scalar layout (numeric scalar, vector, matrix, enum, an
array of those, or a user-defined record made up of those) or it
doesn't (resources, node objects, ray queries, hit objects, dynamic
resources, or aggregates that transitively contain them). Walking both
sets independently invites them to drift out of sync — e.g., a future
intangible-like type added to one walker but forgotten in the other
would silently mis-classify.

I collapsed `IsHLSLIntangibleType` down to
`return !IsHLSLNumericOrAggregateOfNumericType(type)` (with a null
guard preserved for the existing callers). The existing
`IsHLSLCopyableAnnotatableRecord` body that
`IsHLSLNumericOrAggregateOfNumericType` calls already does the same
field-and-base walk that the old hand-written intangible recursion
did, so the behavior on every "real" HLSL type — and on every test in
`is-intangible.hlsl` — is unchanged. The behavior on degenerate
non-HLSL types (function types, `signed char`) shifts to "intangible",
which matches the trait's intent (anything not flattenable to a
numeric scalar sequence).

### `__is_scalar_layout_compatible` and `FlattenedTypeIterator`

`GetHLSLFlattenedScalarTypes` was a second flattener that produced a
`SmallVector<QualType>` of leaves. SemaHLSL already has the canonical
walker — `FlattenedTypeIterator` — which is what overload resolution
and initialization use to compare aggregates. Maintaining a parallel
one is exactly the duplication the prompt called out.

I added `hlsl::IsHLSLScalarLayoutCompatible(Sema&, QualType, QualType)`
in `SemaHLSL.h`, implemented in `SemaHLSL.cpp` next to the iterator
class so it has access to `HLSLExternalSource`. The implementation
constructs two iterators, advances them in lockstep with
`getCurrentElementSize()` runs (so an `int[1000]` doesn't cost 1000
iterations), and accepts each step if the element types are the same
or both arithmetic. Intangible operands are still rejected up front.
`GetHLSLFlattenedScalarTypes` and its header declaration are deleted.

`SemaExprCXX.cpp` now just calls the helper. The
`is-scalar-layout-compatible.hlsl` test continues to pass unchanged
and `ninja check-all` reports the same
4630 / 9 / 33 numbers as before.


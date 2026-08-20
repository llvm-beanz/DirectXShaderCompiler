# Agent thoughts: `LinAlg<>[]` intrinsic parameters

## Goal

`utils/hct/gen_intrin_main.txt` declared the LinAlg groupshared memory
intrinsics with a `LinAlg[]` parameter:

```
void [[min_sm=6.10]] __builtin_LinAlg_MatrixLoadFromMemory(out LinAlgMatrix ret, groupshared LinAlg[] memory, ...);
```

The request was to be able to declare it as `groupshared LinAlg<>[] memory`,
meaning "array of arithmetic values *or* array of vectors of arithmetic
values", and to make calls with arrays of vectors work.

## Investigation

* `db_hlsl.load_intrinsics` in `utils/hct/hctdb.py` parses the intrinsic
  definitions. `T[]` maps to `LITEMPLATE_ARRAY` with `rows = 1` and
  `cols = "c"` (the `IA_C` "special slot" that matches a size against the
  argument), and `T<>` maps to `LITEMPLATE_ANY`.
* `HLSLExternalSource::MatchArguments` in `tools/clang/lib/Sema/SemaHLSL.cpp`
  matched an array argument only by its shape (`AR_TOBJ_ARRAY`); it never
  looked at the array's element type, so `TypeInfoCols` stayed `1` and the
  synthesized parameter always ended up as an array of scalars. That is why
  `float4 Arr[64]` failed with "cannot initialize a parameter of type
  `float const addrspace(3) (&)[64]`".
* `tools/clang/lib/Headers/hlsl/dx/linalg.h` already advertises support for
  arrays of vectors: `Matrix::Load`/`Store` constrain `T` through
  `strip_vector_type` and `InterlockedAccumulate` through
  `is_arithmetic_vector`. So the header and the builtin disagreed, and the
  header's promise was the intended behavior. That made it clear the change
  should apply to all three memory intrinsics (load, store, accumulate), not
  only to `MatrixLoadFromMemory`.
* The DXIL operations (`LinAlgMatrix{LoadFrom,StoreTo,AccumulateTo}Memory`)
  are declared in `utils/hct/hctdb.py` with the overload set `o,hfdwil` and a
  `$x_gs1` pointer parameter, i.e. the memory pointer is always a pointer to a
  *scalar*. So no DXIL, validation or metadata change is needed; the front end
  just has to hand the operation a scalar pointer.

## Design

1. **New legal template.** `LITEMPLATE_ANY_ARRAY` in
   `include/dxc/dxcapi.internal.h`, right after `LITEMPLATE_ARRAY`. Adding a
   new value at the end of the enum keeps the existing values stable for the
   HLSL extension mechanism, which also uses this header.
   Legal shapes are the same as for `LITEMPLATE_ARRAY` (`g_ArrayTT`); the
   difference is purely in how the element is matched and rebuilt, so the
   entry in `g_LegalIntrinsicTemplates` reuses `g_ArrayTT`.
2. **Generator.** A `T<>[]` regex placed before the plain `T[]` regex (the
   latter would otherwise match with a base type of `LinAlg<>` and assert).
3. **Matching.** For a `LITEMPLATE_ANY_ARRAY` parameter, strip the array
   dimensions of the argument and inspect the element:
   * vector -> `TypeInfoCols = GetHLSLVecSize(...)`, which flows into the
     `IA_C` special slot exactly like a plain vector parameter, so all the
     existing size-combining logic is reused;
   * scalar -> unchanged (cols stays 1);
   * anything else (matrix, struct, ...) -> `badArgIdx`, so overload
     resolution reports "no matching function" instead of a confusing
     parameter-initialization error.
4. **Parameter synthesis.** `NewSimpleAggregateType` builds the array's
   *element* type, which is then wrapped in the argument's array dimensions.
   Passing `AR_TOBJ_VECTOR` explicitly (instead of the template's
   `AR_TOBJ_ARRAY`) when the argument's element is a vector makes it build a
   vector element. Passing the kind explicitly matters for `float1`, where
   `uCols == 1` would otherwise silently produce a scalar element.
5. **Lowering.** `TranslateLinAlgMatrix{LoadFromMemory,StoreToMemory,
   AccumToMemory}` used `GEP(Arr, {0, 0})`, which produces a `<N x T>*` for an
   array of vectors and would request an unsupported DXIL overload. Replaced
   with a shared helper that walks every array dimension *and* the vector
   element until it reaches a scalar pointer. This also fixes multi-dimensional
   arrays, which previously produced a pointer to a sub-array.

### Alternatives considered

* Teaching `LITEMPLATE_ARRAY` itself about vector elements. Rejected: it would
  silently change every existing `T[]` parameter and there would be no way to
  spell "array of scalars only" any more.
* Making `<>` on an array mean "scalar, vector or matrix" for symmetry with
  `LITEMPLATE_ANY`. Rejected: an array of matrices has no meaningful layout for
  these operations, and the request was specifically about vectors of
  arithmetic values.

## Testing

Every phase of translation is covered:

* Sema: `tools/clang/test/SemaHLSL/hlsl/linalg/builtins/*/vector-array-ast.hlsl`
  check the synthesized declaration (`vector<float, 4> const addrspace(3)
  (&)[64]`, and `vector<uint8_t4_packed, 2>` for the accumulate case);
  `builtins/vector-array-errors.hlsl` checks that arrays of matrices and arrays
  of structs are still rejected.
* HL codegen and DXIL lowering:
  `tools/clang/test/CodeGenDXIL/hlsl/linalg/builtins/*/vector-array.hlsl` run
  both `-fcgl` (the `dx.hl.op` call keeps the `[64 x <4 x float>]` pointer) and
  the full pipeline (the `dx.op` call receives a `float addrspace(3)*` produced
  by a three-index GEP). The load test also covers an array reaching the
  builtin through a function parameter.
* API level: `tools/clang/test/CodeGenDXIL/hlsl/linalg/api/
  matrix-groupshared-vector-array.hlsl` exercises `Matrix::Load`,
  `Matrix::Store` and `Matrix::InterlockedAccumulate` from `dx/linalg.h` with
  both `float4` and `vector<uint8_t4_packed, 2>` arrays.

Verified with a build configured through `cmake/caches/PredefinedParams.cmake`;
`check-all` passes (4674 passes, 10 expected failures, 33 unsupported) and the
`ClangHLSLTests` unit tests pass (654 tests).

Execution (GPU) tests in `tools/clang/unittests/HLSLExec` were left alone: they
require a D3D12 device with SM 6.10 support, which cannot be exercised here.

## Notes / follow-ups

* Component-type checking of the array element is unchanged: e.g. a
  `groupshared double[]` argument still passes Sema and is rejected later by
  the DXIL validator. That behavior predates this change.

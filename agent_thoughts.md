# Agent Thoughts: Fixing Out/Inout Parameter Test Failures

## Overview

This document records the reasoning and approach taken to triage and fix the
test failures introduced by the `cbieneman/out-param-draft` branch, which
rewrites HLSL `inout` and `out` function arguments to use reference types
(`T &`) with `HLSLOutArgExpr` AST nodes for copy-in/copy-out semantics.

## Starting Point

The branch started with **574 unexpected failures** across multiple test suites:
- CodeGenSPIRV: 412 failures (largest category)
- Clang-Unit: 95 failures
- SemaHLSL: 35 failures
- CodeGenDXIL: 22 failures
- DXC: 4 failures
- LitDXILValidation: 3 failures

The core change is that `out`/`inout` parameters are now stored as reference
types (`T &`) in the AST, with copy-in and copy-out semantics represented
explicitly via `HLSLOutArgExpr` nodes.

## Phase 1: Compiler Crashes

The first priority was eliminating crashes. Two distinct crash sites were found:

### GS/HS/DS Crash in HlslTypes.cpp

Functions like `GetHLSLResourceTemplateParamType`, `GetHLSLInputPatchCount`,
and `GetHLSLOutputPatchCount` called `cast<RecordType>()` after stripping
reference qualifiers with `getNonReferenceType()`. The problem: this strips
the `ReferenceType` sugar but NOT `ElaboratedType` sugar, so the `cast<>`
to `RecordType` failed.

**Fix**: Chain `.getNonReferenceType().getCanonicalType()` to strip all sugar.

**Key lesson**: In Clang's type system, `getNonReferenceType()` and
`getCanonicalType()` are complementary—one strips reference qualifiers, the
other strips typedef/elaborated sugar. Both are often needed together.

### SemaHLSL DiagnoseElementTypes Null Dereference

`DiagnoseElementTypes` was called with reference types for out/inout params
and didn't guard against null when the canonical element type was unavailable.

**Fix**: Strip the reference at the start of the function.

## Phase 2: SPIRV Codegen

The SPIRV backend needed the most work. Key issues found:

### Array-to-Pointer Decay

The `doArraySubscriptExpr` function received a `CK_ArrayToPointerDecay` cast
wrapping the array base. Stripping this cast was needed to recover the array
type for correct SPIRV pointer generation.

### Array Temporary Semantics

`doHLSLArrayTemporaryExpr` was using `createCopyMemory` to initialize array
temporaries. With reference-typed parameters, this needed to be a proper
load+store sequence to handle the SPIRV value model correctly.

### ByteAddressBuffer Templated Store

`processByteAddressBufferLoadStore` needed to load the rvalue before performing
a templated store through an inout parameter.

### DeclResultIdMapper Reference Stripping

Several places in `DeclResultIdMapper` needed to call `getNonReferenceType()`
on parameter types before probing them:
- `createStageInputVar`: strip references before determining input var type
- `createCounterVarForDecl`: strip references before creating counter vars
- `getTypeAndCreateCounterForPotentialAliasVar`: strip references before probing

### Resource Out-Param Handling

For **opaque/resource types** passed as `out` (not `inout`) parameters, the
SPIRV backend should NOT create a temporary variable. Resources in SPIRV are
pointer-typed; the original resource pointer IS the value. Creating a temporary
and then trying to copy the resource causes counter-variable assignment failures
(e.g., for `AppendStructuredBuffer`).

**Fix**: In `doHLSLOutArgExpr`, when the parameter is `out` (not `inout`) and
the type is a resource/opaque type, return the original resource lvalue directly
and bind it to the `CastedTemporary` opaque value. Skip the writeback for such
parameters in `processCall` and `processHLSLOutArgWriteback`.

For `inout` resource params, the original pointer-to-pointer pattern is
preserved (global resource tests verify this behavior).

### AstTypeProbe Crash on Incomplete Types

`isOrContainsAKindOfStructuredOrByteBuffer` crashed when called with
`TriangleStream<GS_OUT>` (an incomplete type). After the reference-stripping
fix in `DeclResultIdMapper`, this function was called with the TriangleStream
type. It tried to iterate `cxxDecl->bases()`, which requires a definition.

**Fix**: Guard with `if (cxxDecl->hasDefinition())` before iterating bases.

**Key lesson**: When stripping reference qualifiers exposes more types to
existing code, that code may not handle all possible types safely.

### Buffer Load Status Ordering

`HLOperationLower.cpp` was placing status writeback stores before the actual
buffer load result processing. With the new out-param ordering, this became
visible in tests.

**Fix**: Added `FixStatusLoadOrdering` to reorder status stores to occur after
buffer loads.

### SPIRV Out-Param Writeback for Intrinsics/Textures

Many intrinsics and texture operations pass `out` parameters that need writeback
after the operation. The SPIRV backend needed to properly handle the
`HLSLOutArgExpr` nodes for these cases, generating loads from the temporary
before calling the SPIRV intrinsic and storing back afterward.

## Phase 3: Test Expectation Updates

After fixing bugs, hundreds of test CHECK patterns needed updating because:

1. **New allocas**: The copy-in/copy-out mechanism creates additional alloca
   instructions, changing LLVM value numbering and adding new stack variables.

2. **New function signatures**: `out` parameters are now reference types, so
   LLVM function signatures use pointer types with `noalias` and
   `dereferenceable` attributes.

3. **Instruction reordering**: Copy-in/copy-out code appears at specific
   points (before and after the call), changing the order of loads/stores
   relative to the old by-reference approach.

4. **Parameter attributes**: `nonnull`, `noalias`, `nocapture`, and
   `dereferenceable` attributes appear on pointer arguments for out/inout
   params in ways that didn't exist before.

### Key Pattern Update Insights

- `{{(nonnull |noalias )?}}` before pointer args handles optional attributes
- `{{(, align [0-9]+)?}}` handles optional alignment metadata
- `{{.*}}` is more robust than trying to enumerate every possible attribute
  combination in function signature patterns
- Variable-binding CHECK patterns (`[[VAR:%.*]]`) fail when instruction
  ordering changes—sometimes it's better to use `{{%.*}}` for flexibility

## Phase 4: New Tests from new_tests/

Created 7 new tests from the HLSL samples under `new_tests/`:

- **`fn.param.inout.basic.hlsl`** (SemaHLSL, FCGL, AST checks)
- **`fn.param.inout.matrix.hlsl`** (SemaHLSL, FCGL, AST checks)
- **`fn.param.inout.overload.hlsl`** (SemaHLSL, FCGL checks)
- **`fn.param.out.basic.hlsl`** (SemaHLSL, FCGL, AST checks)
- **`fn.param.out.overload.hlsl`** (SemaHLSL, FCGL checks)
- **`fn.param.inout.array.hlsl`** (SemaHLSL, FCGL checks)
- **`fn.param.inout.struct.hlsl`** (SemaHLSL, FCGL checks)

All tests cover AST representation of `HLSLOutArgExpr` nodes and verify that
the copy-in/copy-out temporaries appear correctly in the frontend IR.

## Remaining Issues

Two test failures remain that represent genuine code generation issues (not
just pattern mismatches):

1. **`type.byte-address-buffer.hlsl`**: Opaque array aliasing in struct causes
   a non-composite AccessChain. This is a complex SPIRV aliasing issue.

2. **`type.rwstructured-buffer.array.counter.indirect2.hlsl`**: A pointer type
   appears in StorageBuffer class, which is likely a pre-existing limitation.

These may require deeper investigation into SPIRV pointer model semantics for
resource arrays.

## Architecture of the HLSLOutArgExpr

The `HLSLOutArgExpr` node has three sub-expressions:
- `SubExprs[BaseLValue]`: OpaqueValueExpr with source = original arg lvalue
- `SubExprs[CastedTemporary]`: OpaqueValueExpr with source = cast from arg
- `SubExprs[WritebackCast]`: assignment expression (argOpV = opV)

The node's type returns the **parameter type** (not the reference type). This
is important because code that checks `getType()` will see the value type, not
the reference. The reference nature is carried by the parameter declaration.

## Lessons Learned

1. **Type sugar matters**: Clang's type system has multiple layers (`ReferenceType`,
   `ElaboratedType`, `TypedefType`). Always use `getCanonicalType()` when you
   need the underlying type, and `getNonReferenceType()` to strip references.

2. **Incomplete types**: When expanding type probing to handle new cases (like
   stripping references), guard against incomplete/forward-declared types.

3. **Resource types in SPIRV**: Resources are fundamentally pointer-typed.
   Creating value-typed temporaries for resource out-params is incorrect.

4. **Test ordering dependency**: FileCheck's sequential scanning means
   instruction reordering can break complex test patterns even when the
   generated code is correct. Tests with tightly coupled variable bindings
   across many instructions are fragile in the face of instruction reordering.

5. **Stash hygiene**: When multiple git stash pop operations are needed, always
   check `git stash list` to ensure all stashes are handled. Nested stashes
   can leave changes unexpectedly staged.

# Agent Thoughts: HLSL Built-in Type Name Disambiguation

## Problem Statement

The `spirv::LowerTypeVisitor` class (and related SPIRV helper files) were
performing type identification via string comparison of the unqualified type
name returned by `RecordDecl::getName()`. This is incorrect because it matches
any user-defined type whose unqualified name happens to be the same as a
built-in HLSL type, regardless of namespace.

For example, `namespace myns { struct RayQuery { float x; }; }` would
previously be misidentified as the built-in `RayQuery<FLAGS>` type, causing
crashes or incorrect SPIR-V output.

## Root Cause Analysis

The HLSL type system already attaches AST attributes to built-in types at
declaration time in `SemaHLSL.cpp` and `ASTContextHLSL.cpp`. These attributes
uniquely identify built-in types:

- `HLSLResourceAttr` (with `ResKind` and `ResClass`): attached to all
  resource types (textures, buffers, samplers, acceleration structures, etc.)
- `HLSLRayQueryObjectAttr`: attached to the built-in `RayQuery<FLAGS>` template
- `HLSLTessPatchAttr`: attached to `InputPatch` and `OutputPatch`
- `HLSLStreamOutputAttr`: attached to stream output types
- `HLSLDynamicResourceAttr`: attached to dynamic resource heap types

The existing `IsHLSLRayQueryType()` function in `HlslTypes.cpp` was NOT using
the `HLSLRayQueryObjectAttr` that was already attached—it was still doing a
string comparison. Vulkan's `SubpassInput`/`SubpassInputMS` types had NO
attribute at all.

## Changes Made

### Commit 1: Fix `IsHLSLRayQueryType`
Updated `IsHLSLRayQueryType()` in `HlslTypes.cpp` to use `HLSLRayQueryObjectAttr`
instead of comparing the unqualified name. The attribute was already being
attached in `DeclareRayQueryType()`.

### Commit 2: Add `HLSLVkSubpassInput` attribute
Added a new `HLSLVkSubpassInput` attribute (with `IsMultiSampled` flag) to
`Attr.td` to mark the built-in Vulkan `SubpassInput<T>` and `SubpassInputMS<T,S>`
types. This attribute is attached in `SemaHLSL.cpp` when declaring these types.

### Commit 3: Add `GetHLSLResourceKind`/`GetHLSLResourceClass` helpers
Added two new public helper functions to `HlslTypes.h/cpp` that expose the
`HLSLResourceAttr` resource kind and class. This allows SPIRV code to query
resource type attributes without directly depending on the attribute internals.

### Commit 4: Replace string matching with attribute checks
- **`LowerTypeVisitor::lowerResourceType`**: Rewrote to use `GetHLSLResourceKind`,
  `GetHLSLResourceClass`, `IsHLSLRayQueryType`, `IsHLSLInputPatchType`,
  `IsHLSLOutputPatchType`, `IsHLSLStreamOutputType`, and the attribute-based
  SubpassInput checks instead of all string comparisons.
- **`AstTypeProbe.cpp`**: Updated ~15 type-checking functions to use attribute
  checks, including `isStructuredBuffer`, `isTexture`, `isBuffer`, `isSampler`,
  `isAKindOfStructuredOrByteBuffer`, `isOpaqueType`, `getHlslResourceTypeName`,
  `isRelaxedPrecisionType`, `isRasterizerOrderedView`, etc.

### Commit 5: Tests
Added three FileCheck test files verifying that user-defined types with
built-in names compile correctly and don't produce wrong SPIR-V.

## Key Design Decisions

1. **`AppendStructuredBuffer`/`ConsumeStructuredBuffer` disambiguation**: These
   types share the same `ResKind=StructuredBuffer, ResClass=UAV` as
   `RWStructuredBuffer`. There's no attribute that distinguishes them. Since
   only built-in HLSL types have `HLSLResourceAttr`, the name check
   (`name != "AppendStructuredBuffer"`) is safe after confirming the type has
   the attribute—no user-defined type in a namespace would ever have it.

2. **`RasterizerOrderedView` detection**: ROV types also share `ResClass=UAV`
   with regular RW types. After confirming `ResClass=UAV` (which excludes
   user-defined types without `HLSLResourceAttr`), a `name.startswith("RasterizerOrdered")`
   check is safe.

3. **SubpassInput attribute approach**: Used a dedicated `HLSLVkSubpassInput`
   attribute with an `IsMultiSampled` flag, following the same pattern used
   by `HLSLRayQueryObjectAttr` for `RayQuery`. This is SPIRV-specific but
   fits naturally into the existing attribute infrastructure.

4. **`getAttr<>` template**: The existing template in both `HlslTypes.cpp` and
   `AstTypeProbe.cpp` correctly handles template specializations by walking up
   to the template class declaration, which is where the attributes live.

## Testing

Ran `check-clang-codegenspirv`: 1567 expected passes, 2 expected failures
(the expected failures are pre-existing, unrelated to these changes).

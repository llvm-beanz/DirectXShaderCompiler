# Agent Thoughts: Adding int8_t/uint8_t to HLSL SM 6.10

## Overview

This document records the thought process and discoveries made while implementing `int8_t` and
`uint8_t` support for HLSL under shader model 6.10, generating `i8` values in LLVM IR with full
DXIL 1.10 validation, packed cbuffer layout, and typed buffer rejection.

## Phase 1: Core Type System

**Discovery**: `AR_BASIC_INT8`/`AR_BASIC_UINT8` already existed in the codebase but mapped to
32-bit integers. `DXIL::ComponentType::I8`/`U8` were defined but unused. The groundwork was
already laid, just disconnected.

**Approach**: Map `int8_t`/`uint8_t` to C `signed char`/`unsigned char` in Clang AST. This
leverages the existing `SChar`/`UChar` built-in type handling. Added `HLSLScalarType_int8 = 21`
and `HLSLScalarType_uint8 = 22` enum values.

**Key files**: `HlslTypes.h`, `ASTContextHLSL.cpp`, `SemaHLSL.cpp`, `CGHLSLMS.cpp`

**SM guard**: Added `DiagnoseHLSLScalarType` check in `SemaHLSL.cpp` that rejects `int8_t`/
`uint8_t` below SM 6.10 with error "int8_t is only allowed for HLSL shader model 6.10 and above."

**SPIR-V complication**: SPIR-V tests use `uint8_t` as custom type aliases predating our addition.
Fixed by skipping the HLSL int8/uint8 type lookup in `LookupUnqualified` when in SPIR-V mode.

**Legacy data layout**: `kLegacyLayoutString` specifies `i8:32`, meaning `i8` has 4-byte ABI
alignment. This forced explicit size overrides in `AlignBaseOffset` and `AddTypeAnnotation` —
we must return `1` for `SChar`/`UChar` rather than using LLVM's layout-computed sizes.

## Phase 2: DXIL Validator and Operations

**Discovery**: The DXIL validator had `TypesI8` checks that rejected all i8 usage. Gated these
behind `!IsSM610Plus()` so SM 6.10+ shaders can use i8 types.

**RawBuffer operations**: `RawBufferLoad`/`RawBufferStore` overload types in `hctdb.py` were
`"hfd8wil"` (not including i8). Changed to `"hfd8wil"` with `8` → added i8 support. **Never
edit `DxilOperations.cpp` directly** — it is generated from `hctdb.py` via `hctgen.py`.

## Phase 3: Bug Fixes for Structs and Memcpy

**Bug 1: `GetHLOpcode` called on non-HL memcpy**
In `TranslateStructBufSubscriptUser`, the code called `GetHLOpcode(GEP->use_empty() ? CI : ...)`
before checking whether the user was an HL intrinsic. When the user was a plain `memcpy` from
SROA, this asserted. Fixed by moving the `GetHLOpcode` call inside the HLIntrinsic guard.

**Bug 2: `TryReplaceBaseCastWithGep` interfering with memcpy**
SROA calls `MergeGepUse` before `LowerMemcpy`. A `bitcast({i8,i32}* → i8*)` was being transformed
to `GEP {i8,i32}*, 0, 0` because element 0 was i8. This caused `bStructElt=true`, preventing the
memcpy from being added to `memcpySet` for the parent struct. Fixed: skip the transformation when
`NumZeros > 0` AND the bitcast has memcpy users.

**Bug 3: `DxilMutateResourceToHandle` dangling pointers**
`RWStructuredBuffer<int8_t>` crashed because `mutateCandidates` collected Use pointers, then
invalidated them by calling `GEPO->replaceAllUsesWith` mid-loop. Fixed: use
`GEPO->replaceAllUsesWith(newGO)` directly (not per-use), and skip GEP recreation when the
LLVM types are identical (`MTy == Ty`).

## Phase 4: Cbuffer Packing

This was the most complex part. The goal: pack 4 int8_t values into a single 32-bit cbuffer slot.

**Key architectural decision: byte-unit channel encoding**
The existing cbuffer load code used `channel` as an i32-slot index (0-3 for a 16-byte register).
Changed `channel` to be a **byte offset** (0-15) for all types:
- i32 at cbufOffset=4: old `channel=1`, new `channel=4` → `4/4=1` (same slot)
- i16 at cbufOffset=2: old `channel=1`, new `channel=2` → `2/2=1` (same slot)  
- i8 at cbufOffset=1: new `channel=1` → `slot=0, subByte=1` (new capability)

This is backward-compatible for existing types but enables sub-slot byte extraction for i8.

**i8 cbuffer load mechanism**: `cbufferLoadLegacy.i8` is not a valid DXIL op. Instead:
1. Call `cbufferLoadLegacy.i32` for the 32-bit slot containing the byte
2. For `int8_t` (signed): use `(val << (24 - subByte*8)) >> 24` (arithmetic shift = sign extend)
3. For `uint8_t` (unsigned): use `lshr(val, subByte*8) & 0xFF` (logical shift = zero extend)

**Top-level cbuffer variable type override**
Top-level cbuffer variables are represented as `i32` globals in LLVM host layout (because
`kLegacyLayoutString` has `i8:32` = 4-byte alignment for i8). The `AllocateDxilConstantBuffer`
function uses `C->GetHLSLType()->getPointerElementType()` to get the LLVM type, which returns
`i32` for int8_t. Added: when `fieldAnnotation.GetCompType().Is8Bit()`, override `Ty` to
`llvm::Type::getInt8Ty()` so the alignment computation uses 1 byte.

**The `bNeedNewRow` min-precision bug**
Root cause: `AlignCBufferOffset` computes `bMinPrec = bMinPrecMode && Ty->getScalarSizeInBits() < 32`.
When `bMinPrecMode = true` (default for many profiles when `-enable-16bit-types` is absent),
8-bit types satisfy `< 32` and are incorrectly classified as min-precision types. Min-precision
types in DXIL are specifically 16-bit (half, min16float, min16int, etc.) — not 8-bit.

The consequence: after processing a struct (bCurRowIsMinPrec=false), an i8 variable with
bMinPrec=true triggers `bNeedNewRow |= (false != true) = true`, forcing a new 16-byte row.

**Fix**: Changed condition to `Ty->getScalarSizeInBits() == 16` — only 16-bit types are
min-precision. This is the correct semantic: i8 is a full-precision type in DXIL 1.10.

## Phase 5: Testing

Six test files created:
1. `int8_operators.hlsl`: arithmetic operators, sizeof, min/max, bitwise ops on int8_t/uint8_t
2. `int8_vectors.hlsl`: int8_t2/3/4, uint8_t2/3/4 vector types in structured buffers  
3. `int8_structured_buffer.hlsl`: RWStructuredBuffer<int8_t>, verifies rawBufferLoad/Store.i8
4. `int8_cbuffer.hlsl`: packed cbuffer layout with struct containing mixed int8_t/int fields,
   plus top-level int8_t and uint8_t variables; verifies correct offsets and load patterns
5. `int8_errors.hlsl`: verifies SM < 6.10 rejection
6. `int8_errors_typed_buf.hlsl`: verifies typed buffer (Buffer<int8_t>) rejection

## Key Design Decisions

1. **Use `shl/ashr` for signed, `lshr/and` for unsigned byte extraction** — avoids generating
   actual `i8` values in intermediate IR (which could cause issues in some passes); instead
   keeps arithmetic at i32 level with sign/zero extension.

2. **Never edit `DxilOperations.cpp` directly** — it is regenerated from `hctdb.py`. Always
   modify `hctdb.py` and regenerate.

3. **Byte-unit channel encoding is backward-compatible** — existing i32/i16 extraction is
   unaffected; the change only adds new capability for sub-4-byte values.

4. **SPIR-V mode exclusion** — SPIR-V frontend has its own `uint8_t` type alias that predates
   HLSL SM 6.10. Guard the HLSL int8/uint8 type registration behind `!context.getLangOpts().SPIRV`.

## Phase 6: Test Improvement Pass (COPILOT-TODO Feedback)

### Pre-existing test failures

`int8_errors.hlsl` and `int8_errors_typed_buf.hlsl` failed because lit sets
`-o pipefail` and dxc exits with code 5 when it emits compilation errors.
The fix is to use `not %dxc` (LLVM's `not` tool inverts the exit code), which
is the standard pattern throughout the LLVM/clang test suite.

### Ground-truth-first approach

Rather than guessing CHECK patterns, I compiled every test case with `dxc` and
read the actual DXIL output before writing a single FileCheck line.  Key
findings that differed from the original test expectations:

- **int8_t vector rawBuffer ops use v2i32/v4i32**, not v2i8/v4i8.  The
  rawBufferVectorLoad/Store overload bitmask 0x4e7 excludes the i8 bit (0x10).
- **Scalar int8_t fields in StructuredBuffers are 4-byte padded**.  Multiple
  consecutive int8_t fields are NOT packed together.
- **int8_t4 in a StructuredBuffer is 4-byte aligned**, not 16-byte.
- **cbuffer int8_t arrays**: each element in its own 16-byte slot, but a
  subsequent non-array field can pack into the unused portion of the last slot.
- **RWByteAddressBuffer**: stores use `rawBufferStore.i8`; loads use
  `rawBufferLoad.i32` + `trunc`.
- **Explicit int8→int32 sign-extend**: `shl i32 N, 24` + `ashr i32 N, 24`.
  **uint8→uint32 zero-extend**: `and i32 N, 255`.
- **Implicit narrowing** produces `-Wconversion`:
  `"conversion from larger type 'int' to smaller type 'signed char'"`.

### Compiler bug discovered and fixed

While running `int8_cbuffer_packing.hlsl` against the debug build
(PredefinedParams.cmake has empty `CMAKE_BUILD_TYPE`, enabling assertions),
an assertion fired in `AlignCBufferOffset`:

```cpp
DXASSERT(scalarSizeInBytes == 1 || !(offset & 1), "otherwise we have an invalid offset.");
```

Root cause: `int8_t arr[4]` leaves offset at 48+1=49 (odd), then the
subsequent `int` field calls `AlignCBufferOffset` with offset=49 and type=i32.
`AlignBufferOffsetInLegacy` correctly rounds 49 → 52, so the assertion was
firing on valid input.  The assertion pre-dated int8_t support and assumed
no type smaller than 16 bits could produce an odd offset.  Fix: remove the
assertion and add a comment explaining the new invariant.

### New test files

- `int8_cbuffer_packing.hlsl`: cbuffer vector and array packing rules.
- `int8_conversions.hlsl`: explicit scalar/vector conversion patterns.
- `int8_implicit_conversions.hlsl`: `-Wconversion` diagnostic tests.

### Lessons learned

1. Always compile test inputs against the real compiler before writing
   FileCheck patterns.  The DXIL emitted for int8_t is often surprising
   (e.g., vector widening to i32).
2. Debug builds catch real bugs.  The PredefinedParams.cmake build with
   assertions enabled found a spurious `DXASSERT` that release builds
   silently ignored.
3. The LLVM `not` tool is the correct way to write tests that expect compiler
   errors; `set -o pipefail` makes any non-zero dxc exit propagate otherwise.

## Phase 7: Addressing COPILOT-TODO Feedback and char/unsigned char Support

### COPILOT-TODO items addressed

Four test files had `// COPILOT-TODO:` comments requesting fixes:

1. **int8_errors.hlsl** and **int8_errors_typed_buf.hlsl** and
   **int8_implicit_conversions.hlsl** — all requested conversion from
   FileCheck to Clang's `-verify` mechanism for diagnostic tests.

2. **int8_vectors.hlsl** — requested adding native i8 vector overloads to
   `rawBufferVectorLoad`/`rawBufferVectorStore` and updating the test.

### Fix: duplicate SM diagnostics in LookupUnqualified

**Discovery**: When DXC compiled `int8_t bad_var;` with SM < 6.10, the error
"int8_t is only allowed for HLSL shader model 6.10 and above." fired three
times at the same source location. This made `-verify` impossible since a
single `// expected-error` would only match one occurrence.

**Root cause**: `LookupUnqualified` was returning `false` after emitting the
SM error. Clang's lookup retry logic then repeated the lookup multiple times,
re-emitting the diagnostic each time.

**Fix**: Changed `LookupUnqualified` in `SemaHLSL.cpp` to add the typedef to
the lookup result even when `DiagnoseHLSLScalarType` returns false (i.e., when
the SM check fails). The error is still emitted exactly once. Returning `true`
prevents Clang from retrying the lookup.

### Fix: native i8 vector overloads for rawBufferVectorLoad/Store

**Discovery**: `isMinPrecisionType` in `HLOperationLower.cpp` incorrectly
classified i8 as a min-precision type due to the HLSL legacy data layout
string `i8:32` (32-bit ABI size vs 8-bit primitive size). This caused int8_t
vectors to be widened to i32 vectors before using `rawBufferVectorLoad/Store`,
so the native i8 overloads were never reached.

**Fix 1**: Added an early return in `isMinPrecisionType` to exclude i8 (and
preserve the existing i1 exclusion) from min-precision classification.

**Fix 2**: Updated `hctdb.py` and `DxilOperations.cpp` to add i8 (`8`) to the
`oload_types` string for both `RawBufferVectorLoad` and `RawBufferVectorStore`,
changing the bitmasks from `0x4e7`/`0xe7` to `0x4f7`/`0xf7`.

**Result**: `int8_t2` vectors now use `rawBufferVectorLoad.v2i8` / 
`rawBufferVectorStore.v2i8` (native), and `uint8_t4` uses `v4i8`. The element
stride in the structured buffer still reflects the host HLSL layout (i8:32),
so `int8_t2` → stride=8, `uint8_t4` → stride=16.

### Feature: char and unsigned char as int8_t/uint8_t alternatives

**Request**: Add support for `char` and `unsigned char` as aliases for
`int8_t` and `uint8_t` respectively, starting from SM 6.10.

**Discovery**: `char` was unconditionally reserved in HLSL in `ParseDecl.cpp`
via `goto HLSLReservedKeyword`. Plain `char` maps to `BuiltinType::Char_S`
(on signed-char platforms like x86), while `unsigned char` produces
`BuiltinType::Char_U`.

**Changes**:
- `ParseDecl.cpp`: Made `kw_char` conditional — only reserved below SM 6.10.
  For SM 6.10+, falls through to normal type specifier handling.
- `SemaHLSL.cpp`: Added `Char_S` → `AR_BASIC_INT8` and `Char_U` →
  `AR_BASIC_UINT8` to the type mapping switch.
- `CGHLSLMS.cpp`: Added `Char_S`/`Char_U` cases alongside `SChar`/`UChar`
  in `GetTypeInfo`, cbuffer offset calculation, and annotation size.

**Lesson**: Plain `char` vs `signed char` produce different type names in
diagnostic messages: `int8_t` shows as `'signed char'`, while `char` shows
as `'char'`. This is expected behavior in Clang.

### New test files

- `int8_char_types.hlsl`: Verifies `char`/`unsigned char` produce
  `rawBufferLoad/Store.i8`, `sext`/`zext` for widening, in SM 6.10.
- `int8_char_types_errors.hlsl`: Verifies `char` is rejected with
  `'char' is a reserved keyword in HLSL` in SM < 6.10.
- `int8_char_implicit_conversions.hlsl`: Verifies narrowing warnings for
  `int → char` and `uint → unsigned char` conversions.

## Phase 8: SPIRV SPV_KHR_8bit_storage Support

### Goal

Add proper SPIRV support for `int8_t`/`uint8_t` when SM 6.10 is specified, emitting the
`SPV_KHR_8bit_storage` extension and the required capabilities.

### Changes made

#### 1. `FeatureManager.h` — Extension enum

Added `KHR_8bit_storage` before `KHR_16bit_storage` in the `Extension` enum.

#### 2. `FeatureManager.cpp` — Registration and suppression

- `getExtensionSymbol`: maps `"SPV_KHR_8bit_storage"` → `Extension::KHR_8bit_storage`
- `getExtensionName`: reverse mapping
- `isExtensionRequiredForTargetEnv`: added Vulkan 1.2 check — `SPV_KHR_8bit_storage` was
  promoted to SPIR-V 1.5 core (Vulkan 1.2), so the `OpExtension` instruction is suppressed
  for Vulkan 1.2+ targets (capabilities are still emitted).

#### 3. `CapabilityVisitor.cpp` — Capability emission

In `addCapabilityForType()`, extended the struct-type handling block to detect 8-bit integer
members via `SpirvType::isOrContainsType<IntegerType, 8>`:

- `PushConstant` storage class → `StoragePushConstant8`
- `UniformBuffer` interface → `UniformAndStorageBuffer8BitAccess`
- `StorageBuffer` interface → `StorageBuffer8BitAccess`

This mirrors the existing 16-bit handling in the same function.

#### 4. `AlignmentSizeCalculator.cpp` — Layout for 8-bit types

Added `SChar`, `Char_S`, `UChar`, `Char_U` cases (the AST types for `int8_t` / `uint8_t` /
`char` / `unsigned char`) returning `{1, 1}` (1-byte alignment, 1-byte size). Without this,
the fallthrough to `emitError("unimplemented")` caused compilation errors.

#### 5. `SemaHLSL.cpp` — Enable int8_t/uint8_t as HLSL built-ins in SPIRV/SM 6.10

`LookupUnqualified` previously returned `false` for `int8_t`/`uint8_t` in SPIRV mode to let
users define their own aliases via `vk::SpirvType`. Updated the condition to only bypass for
SM < 6.10. For SM 6.10+, the HLSL built-in types are used, enabling proper SPIRV codegen.

Existing SPIRV tests (SM 6.0 + inline `uint8_t` via `vk::SpirvType`) are unaffected.

### New test files (CodeGenSPIRV)

| File | Checks |
|------|--------|
| `vk.layout.8bit-types.cbuffer.hlsl` | `UniformAndStorageBuffer8BitAccess`, extension, byte offsets |
| `vk.layout.8bit-types.sbuffer.hlsl` | `StorageBuffer8BitAccess`, extension, byte offsets, stride=8 |
| `vk.layout.8bit-types.pc.hlsl` | `StoragePushConstant8`, extension, byte offsets |
| `vk.layout.8bit-types.cbuffer.vk12.hlsl` | Capability present, extension suppressed for Vulkan 1.2 |

### Key insight

The SPIRV `LowerTypeVisitor.cpp` already mapped `SChar`/`UChar` to 8-bit SPIRV integer types.
The `CapabilityVisitor.cpp` already emitted `Int8` capability. What was missing was:
1. The extension name registration
2. The storage-context-sensitive capability emission
3. The alignment/size calculation for SPIRV layout
4. Lifting the SM 6.10 guard in SPIRV's `LookupUnqualified`

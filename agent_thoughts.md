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

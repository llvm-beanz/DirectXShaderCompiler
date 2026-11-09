# Shader printf

This page describes shader debug printing for application developers, including API usage and binary layouts for custom decoders.

The shader API writes arguments into a UAV. The compiler stores format strings in the shader binary. A host decoder combines these inputs after GPU completion.

## Current Limitations

* `dx::printf` supports DirectX targets only. Including `<dx/printf.h>` for SPIR-V targets produces an error.
* Library shaders are not supported.
* Binary layout is not version stable. Use matching shader headers, compiler, and decoder.

The per-lane string-offset layout is incompatible with earlier layouts that stored one string offset in the wave header.
Rebuild shaders and update decoders together.

## Shader API

The public entry points are:

```hlsl
namespace dx {
  void InitializeDebugStream();

  template <typename... T>
  void printf(string Str, T... Args);
}
```

The header also declares the output resource:

```hlsl
RWByteAddressBuffer DebugOutput : register(HLSL_DX_PRINTF_BINDING);
```

If not defined explicitly, `HLSL_DX_PRINTF_BINDING` is defined to `u0, space9`.
This is used as the default binding for the string output buffer.

`dx::printf` returns no value. It records arguments without formatting them on the GPU.
It does not initialize the stream, enforce buffer capacity, or append a newline.

### Example

Save this shader as `example.hlsl`:

```hlsl
#include <dx/printf.h>

[numthreads(64, 1, 1)]
void main(uint3 ThreadID : SV_DispatchThreadID) {
  dx::InitializeDebugStream();

  uint Index = ThreadID.x;
  dx::printf("thread %u: value=%f bits=%x\n",
             Index, float(Index) * 0.5f, Index);

  if (Index == 0) {
    dx::printf("finished\n");
  }
}
```

Compile the shader:

```console
dxc -T cs_6_0 -E main -HV 202x -Fo example.dxil example.hlsl
```

The root signature must expose a UAV binding for register `u0` in register space `9`.
For example, a descriptor-table root parameter can use `DescriptorTable(UAV(u0, space=9))`.
The register space does not identify a root parameter index.

### Format strings and argument types

The format string can be a literal or a runtime selection among literals.
Conditional assignments and loop-carried strings are supported through compiler `select` and `phi` graphs.
Local variables, aggregate fields, and helper parameters are supported after inlining and scalarization.
String loads that remain after these passes are unsupported, including dynamically indexed arrays of string-bearing aggregates.
Runtime-created character data is unsupported.

Each lane records its own format string offset.
Different lanes at the same call can select different formats without changing the argument count or types.
The variadic template fixes the argument layout for that call.
The selected format must interpret that layout correctly.

For example:

```hlsl
string Format = "even thread %u\n";
if (Index & 1)
  Format = "odd thread %u\n";
dx::printf(Format, Index);
```

The compiler does not match conversions against argument types or insert C varargs promotions.
For the supplied host formatter, use explicit 32-bit `int`, `uint`, and `float` arguments.
For example, pass `float(Value)` for `%f`, not an integer or a `double`.

The variadic writer uses typed `RWByteAddressBuffer.Store<T>` operations.
Other storable types require a custom formatter that knows their exact HLSL storage layout.
Strings are not storable argument values, and the supplied formatter has no `%s` conversion.

## Host resource setup and readback

1. Allocate a UAV-capable buffer with a byte size divisible by four.
2. Reserve four bytes for the stream counter, plus space for every record.
3. Create and bind a raw UAV at `u0, space9`.
4. Set the first four bytes to zero before starting a new capture.
5. Order the initialization write before shader access using the required resource transitions or UAV barriers.
6. Call `dx::InitializeDebugStream()` before any `dx::printf()` calls in each participating shader invocation.
7. Run the GPU workload.
8. Order the shader writes before copying the buffer into a readback resource.
9. Wait for GPU completion before mapping and decoding the readback data.
10. Decode with the exact shader container used for the capture.

For a D3D12 raw UAV descriptor, use `DXGI_FORMAT_R32_TYPELESS`, `D3D12_BUFFER_UAV_FLAG_RAW`, and `StructureByteStride = 0`.
Set `NumElements` to the exposed byte capacity divided by four.
The counter is an ordinary word inside this buffer, not a separate D3D12 UAV counter resource.

Only the counter needs clearing for a new capture. Bytes beyond the recorded stream length are ignored.

`InitializeDebugStream()` atomically changes a zero counter to four from the first active lane of each wave.
It leaves a nonzero counter unchanged. It neither clears old records nor resets a previous capture.
Calling it unconditionally at shader entry avoids initialization inside divergent control flow.

### Capacity and capture boundaries

There is no GPU-side bounds guard, overflow flag, or truncation protocol.
The counter advances by the full reservation even where the resource cannot hold the record.
An undersized resource does not produce a valid truncated stream. Clamping the counter cannot repair missing records.

The required byte capacity is:

```text
4 + sum over records (8 + LaneCount * LaneSize)
```

`LaneSize` includes a four-byte string offset followed by the lane's argument bytes.
Each active wave reaching a call normally contributes one record.
Divergence changes the active lane count and can increase the number of records.
A conservative bound reserves a separate eight-byte record header for each printing invocation, plus its lane block.
The total counter value must also fit in an unsigned 32-bit integer without wrapping.

The stream contains no shader identifier or capture identifier.
Keep output from shaders with different string tables in separate captures.
Otherwise, a string offset cannot identify its originating shader.
Resetting a shared counter during an active capture is invalid.

## Supplied C++ decoder

[`<dxc/shader_printf.h>`](../include/dxc/shader_printf.h) is a standalone, header-only C++ decoder.
It requires no DXC or LLVM linkage and can be copied into a host application.

```cpp
std::error_code dxc::PrintDebugStream(
    const void *Buffer, std::size_t BufferSize,
    const void *Shader, std::size_t ShaderSize,
    std::ostream &Out);
```

`Buffer` points to the resource bytes, starting with the counter.
`BufferSize` is the available readback byte count, not a count of words.
`Shader` points to the complete DXIL container, not extracted LLVM bitcode.
`ShaderSize` is that container's available byte count.
`Out` receives the concatenated formatted messages.

```cpp
#include <dxc/shader_printf.h>
#include <iostream>

bool PrintCapture(const void *Readback, std::size_t ReadbackSize,
                  const void *Shader, std::size_t ShaderSize) {
  const std::error_code Error =
      dxc::PrintDebugStream(Readback, ReadbackSize,
                           Shader, ShaderSize, std::cout);
  if (Error) {
    std::cerr << "Shader printf: " << Error.message() << '\n';
    return false;
  }
  return true;
}
```

A zero-valued `std::error_code` indicates success.
Errors use `dxc::ShaderPrintfError` and `dxc::ShaderPrintfErrorCategory()`, whose category name is `dxc.shader_printf`.
Examples include malformed containers, missing string tables, invalid stream lengths, truncated arguments, and output errors.
Earlier messages can already be written before a later record produces an error.
The decoder requires a valid, nonempty string table even for an empty stream.

### Supported formatting

| Conversion | Argument interpretation | Output |
|---|---|---|
| `%d` | One signed 32-bit two's-complement integer | Decimal |
| `%u` | One unsigned 32-bit integer | Decimal |
| `%f` | One IEEE-754 binary32 value | Scientific notation, six digits after the decimal point |
| `%x` | One unsigned 32-bit integer | Lowercase hexadecimal with `0x` and at least six zero-padded digits |
| `%%` | No argument | A literal `%` |

For example, `%f` formats `1.5f` as `1.500000e+00`, and `%x` formats `0xab` as `0x0000ab`.
Larger hexadecimal values are not truncated.
Formatting uses the classic C++ locale, independent of the output stream's locale and numeric formatting state.

Each numeric conversion consumes exactly four bytes.
An unrecognized conversion is copied literally and consumes no argument.
A trailing `%` is also copied literally.
Width, precision, flags, length modifiers, vectors, and additional conversion letters have no special support.
For example, `%08x` remains literal text rather than formatting an argument.

Unused argument bytes are skipped at the end of each lane.
A recognized conversion without four available argument bytes produces an error.
Messages have no automatic separators, lane labels, or newlines.

## String data in the shader binary

### Locating the `STAB` container part

All multibyte integers in the container and debug stream use little-endian byte order.
The DXIL container begins with the following 32-byte header:

| Byte offset | Size | Meaning |
|---|---|---|
| 0 | 4 | ASCII `DXBC` |
| 4 | 16 | Container hash |
| 20 | 2 | Major version, currently `1` |
| 22 | 2 | Minor version, currently `0` |
| 24 | 4 | Total container size in bytes |
| 28 | 4 | Part count |
| 32 | `4 * PartCount` | Array of part offsets, relative to the container start |

Each part offset identifies an eight-byte part header:

| Relative byte offset | Size | Meaning |
|---|---|---|
| 0 | 4 | Part FourCC |
| 4 | 4 | Payload byte count, excluding this header |
| 8 | Payload byte count | Part payload |

The string table part has the FourCC `STAB`, or `0x42415453` as a little-endian integer.
Its payload is the string table itself, without another header, string count, or offset array.
Each lane's `StrOffset` is relative to the first payload byte.
It is not relative to the shader container, the part header, or the DXIL program.

For a custom container reader:

1. Verify the signature, version, declared container size, and part-offset array bounds.
2. Read each part header through the offset array.
3. Verify that every header and payload fits within the declared container size.
4. Locate the `STAB` part and retain its payload pointer and byte count.

The supplied decoder also rejects out-of-order or overlapping parts and containers larger than `0x80000000` bytes.
It does not authenticate the container hash.

### String table layout

The payload is a byte string table beginning with a NUL byte.
Each referenced format string is NUL-terminated.
The payload is padded with zero bytes to a multiple of four bytes.
Individual strings and their offsets do not require four-byte alignment.

The compiler collects strings passed to `dx::__builtin_hlsl_string_to_offset(string)`.
It replaces each call with an unsigned 32-bit byte offset into this table.
Literal arguments produce constant offsets.
Runtime selections produce integer `select` and `phi` graphs over those offsets.
`dx::printf` uses this builtin internally.
The builtin also supports custom shader-side message writers with the same literal-backed selection and non-library restrictions.
The table does not necessarily contain every string literal in the source.

Identical strings share storage. Suffix strings can share the trailing bytes of longer strings.
Table order does not follow source order, and offsets can change after recompilation.
A decoder must use the recorded offset, not enumerate strings or reconstruct compiler ordering.

The current lowering preserves the source literal's terminating NUL and adds a table-entry terminator.
Extra NUL bytes and final alignment padding are therefore normal.
For example, a table containing only `"hello"` has these bytes:

```text
Payload offset:  00 01 02 03 04 05 06 07
Byte value:      00 68 65 6c 6c 6f 00 00
                    h  e  l  l  o
```

The `"hello"` offset is `1`, and the payload size is eight bytes.
An empty string can reference another NUL byte rather than offset zero.
An embedded NUL terminates the format string as seen by the supplied decoder.

The table contains narrow string bytes, not UTF-16 code units.
It has no text-encoding marker. A custom formatter must interpret non-ASCII bytes according to its application's encoding convention.

The same payload is preserved in DXIL bitcode metadata named `dx.strtab`.
That named metadata contains one node with one metadata string holding the payload bytes.
This representation supports disassembly and reassembly.
The host decoder reads the container's `STAB` part without parsing bitcode metadata.

## Print messages in the resource

### Stream header

The first four resource bytes contain an unsigned 32-bit stream byte count:

| Byte offset | Type | Meaning |
|---|---|---|
| 0 | `uint32` | End offset of reserved records, including this four-byte header |
| 4 | Records | Consecutive wave records |

A value of `0` represents an uninitialized empty stream.
A value of `4` represents an initialized empty stream.
A nonempty valid stream has a four-byte-aligned count no larger than the available resource bytes.
There is no separate record count.

The counter is an allocation cursor, not a completion signal.
GPU completion and resource synchronization remain necessary before reading it.

### Wave record

Each record starts with the shader header's `MessagePrefix`:

| Relative byte offset | Type | Shader field | Meaning |
|---|---|---|---|
| 0 | `uint32` | `LaneCount` | Number of active lanes participating in this call |
| 4 | `uint32` | `Size` | Bytes per lane, including its string offset, called `LaneSize` below |
| 8 | Bytes | Lanes | `LaneCount` consecutive lane blocks |

The record byte size is `8 + LaneCount * LaneSize`.
The next record starts immediately after the final lane block.
Zero-argument calls have an eight-byte record header and a four-byte string offset per lane.
Each lane's selected format is printed once.

The first active lane reserves the entire record with `InterlockedAdd` on the stream counter.
It writes the prefix and broadcasts the reservation offset with `WaveReadLaneFirst`.
Each participating lane writes its own string offset and arguments.

The lane block index is `WavePrefixSum(1)`, and `LaneCount` is `WaveActiveSum(1)`.
Blocks follow active-lane order, with inactive lanes omitted.
The index is not a dispatch thread identifier or an absolute hardware lane index.
Thread identifiers must be passed explicitly as arguments.

Records follow atomic reservation order, not a guaranteed global thread or wave order.
A decoder prints each record's lanes before advancing to the next record.

### Lane block

Each lane block starts with its format string offset:

| Relative byte offset | Type | Meaning |
|---|---|---|
| 0 | `uint32` | `StrOffset`, a format string byte offset in the `STAB` payload |
| 4 | Bytes | Argument data |

For argument types `T0` through `Tn`, the shader computes:

```text
ArgumentSize = sum over arguments (4 * ceil(sizeof(Ti) / 4))
LaneSize = 4 + ArgumentSize

LaneStart = RecordStart + 8 + LaneIndex * LaneSize
ArgumentOffset(j) = LaneStart + 4 + sum over i < j (4 * ceil(sizeof(Ti) / 4))
```

The shader writes `StrOffset` at `LaneStart`.
Each argument is written with `DebugOutput.Store<Ti>(ArgumentOffset(i), Value)`.
Every argument starts on a four-byte boundary.
Padding between arguments is not explicitly initialized.
The bytes follow typed raw-buffer storage rules, not a C varargs ABI or a host structure layout.

There are no per-argument type tags, argument counts, or argument offsets in the record.
The format string or an application-defined schema must supply that information.
For scalar 32-bit arguments, values occupy consecutive four-byte words without padding.
An IEEE-754 float is stored as its binary32 bits, not converted to an integer.

For wider values, vectors, aggregates, or native 16-bit types, a custom decoder needs the exact type and storage layout.
The supplied formatter always advances four bytes per numeric conversion, regardless of the shader argument type.

### Worked record

Suppose `STAB` contains `%u:%f\n` at offset `1` and `%u=%f\n` at offset `9`.
Two active lanes print `(7u, 1.5f)` and `(9u, -2.0f)`.
The first lane selects the first format, and the second lane selects the second format.
The complete stream contains these little-endian words:

| Resource byte offset | Word value | Meaning |
|---|---|---|
| 0 | `0x00000024` | Total stream size: 36 bytes |
| 4 | `0x00000002` | Two active lanes |
| 8 | `0x0000000c` | Twelve bytes per lane, including the string offset |
| 12 | `0x00000001` | First lane's format string offset |
| 16 | `0x00000007` | First lane's unsigned integer |
| 20 | `0x3fc00000` | First lane's binary32 `1.5f` |
| 24 | `0x00000009` | Second lane's format string offset |
| 28 | `0x00000009` | Second lane's unsigned integer |
| 32 | `0xc0000000` | Second lane's binary32 `-2.0f` |

The supplied formatter emits:

```text
7:1.500000e+00
9=-2.000000e+00
```

## Writing a custom decoder

1. Locate and verify the `STAB` payload in the matching shader container.
2. Read the stream byte count with an unaligned-safe little-endian reader.
3. Treat zero as an empty stream.
4. Verify that a nonzero count is at least four, four-byte-aligned, and within the available buffer.
5. Start the record cursor at byte four.
6. Read `LaneCount` within the recorded stream bounds.
7. Stop at a zero lane count for compatibility with the supplied decoder.
8. Verify that a nonzero record has all eight prefix bytes.
9. Read `LaneSize`.
10. Verify that `LaneSize` is at least four and four-byte-aligned.
11. Verify that `LaneSize <= (StreamSize - RecordStart - 8) / LaneCount`.
12. Read each lane's `StrOffset` and verify that it identifies a NUL-terminated string within `STAB`.
13. Format each lane using its selected format and its remaining `LaneSize - 4` argument bytes.
14. Advance the cursor by `8 + LaneCount * LaneSize`.
15. Repeat from step six until the cursor equals the stream byte count.

The writer does not emit a zero-lane terminator. The supplied decoder nevertheless accepts one and ignores the remaining recorded bytes.
The division-based size comparison avoids multiplication overflow.
Each conversion must also fit within its lane block, even where additional bytes exist elsewhere in the stream.

To match the supplied formatter, preserve unknown conversions, skip unused lane bytes, and concatenate messages without extra separators.
For a different formatting language, keep the resource framing separate from argument interpretation.
The binary protocol does not require the supplied conversion set.

## Implementation references

- [Shader writer and API](../tools/clang/lib/Headers/hlsl/dx/printf.h)
- [Standalone host decoder](../include/dxc/shader_printf.h)
- [String-to-offset lowering](../lib/HLSL/DxilStringLowering.cpp)
- [String table deduplication and suffix sharing](../lib/MC/StringTableBuilder.cpp)
- [Container structures and FourCC values](../include/dxc/DxilContainer/DxilContainer.h)
- [Host decoder examples and malformed-input tests](../unittests/DxcSupport/ShaderPrintfTest.cpp)

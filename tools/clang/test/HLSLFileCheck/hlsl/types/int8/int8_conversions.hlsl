// RUN: %dxc -E main -T cs_6_10 %s | FileCheck %s

// Test explicit conversions between int8_t/uint8_t and wider integer types.
//
// Explicit narrowing conversions (int32->int8, int4->int8_t4) truncate.
// Explicit widening conversions sign-extend (int8->int32) or zero-extend
// (uint8->uint32).

RWStructuredBuffer<int8_t>  i8buf   : register(u0);
RWStructuredBuffer<uint8_t> u8buf   : register(u1);
RWStructuredBuffer<int>     i32buf  : register(u2);
RWStructuredBuffer<uint>    u32buf  : register(u3);
RWStructuredBuffer<int8_t4> i8v4buf : register(u4);
RWStructuredBuffer<int4>    i32v4buf : register(u5);

[numthreads(1, 1, 1)]
void main() {
  int8_t  a  = i8buf[0];
  uint8_t ua = u8buf[0];

  // Explicit int8 -> int32: sign-extend via shl 24 / ashr 24.
  // CHECK: shl i32 {{.*}}, 24
  // CHECK: ashr {{.*}} i32 {{.*}}, 24
  // CHECK: rawBufferStore.i32
  int b = (int)a;
  i32buf[0] = b;

  // Explicit uint8 -> uint32: zero-extend via and 255.
  // CHECK: and i32 {{.*}}, 255
  // CHECK: rawBufferStore.i32
  uint ub = (uint)ua;
  u32buf[0] = ub;

  // Explicit int32 -> int8: truncate.
  // CHECK: trunc i32 {{.*}} to i8
  // CHECK: rawBufferStore.i8
  int big = i32buf[1];
  i8buf[1] = (int8_t)big;

  // Explicit int8 -> uint8: same bit pattern, no IR conversion.
  // CHECK: rawBufferStore.i8
  u8buf[1] = (uint8_t)a;

  // Explicit uint8 -> int8: same bit pattern, no IR conversion.
  // CHECK: rawBufferStore.i8
  i8buf[2] = (int8_t)ua;

  // Vector: explicit int8_t4 -> int4 sign-extends each element.
  // The i8 range values are sign-extended from their i8 representation to i32.
  // CHECK: shl <4 x i32> {{.*}}, <i32 24, i32 24, i32 24, i32 24>
  // CHECK: ashr <4 x i32>
  // CHECK: rawBufferVectorStore.v4i32
  int8_t4 sv = i8v4buf[0];
  int4 wv = (int4)sv;
  i32v4buf[0] = wv;

  // Vector: explicit int4 -> int8_t4, stored back as widened <4 x i32>.
  // CHECK: rawBufferVectorLoad.v4i32
  // CHECK: rawBufferVectorStore.v4i32
  int4 bv4 = i32v4buf[1];
  i8v4buf[1] = (int8_t4)bv4;
}

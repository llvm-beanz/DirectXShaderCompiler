// RUN: %dxc -E main -T cs_6_10 %s | FileCheck %s

// Test int8_t/uint8_t in StructuredBuffer and RWStructuredBuffer.
// Verifies that i8 values are loaded/stored using rawBufferLoad/Store.i8.

// Struct with int8_t as first field
struct S1 {
  int8_t x;   // offset 0
  int z;      // offset 4
};

// Struct with int8_t as last field
struct S2 {
  int z;      // offset 0
  int8_t x;   // offset 4
};

StructuredBuffer<S1>    sbuf1 : register(t0);
RWStructuredBuffer<S1>  ubuf1 : register(u0);
StructuredBuffer<S2>    sbuf2 : register(t1);
RWStructuredBuffer<S2>  ubuf2 : register(u1);
RWStructuredBuffer<int8_t>  scalarbuf : register(u2);

[numthreads(1, 1, 1)]
void main() {
  // Scalar int8_t store
  // CHECK: rawBufferStore.i8({{.*}}, i8 42,
  scalarbuf[0] = (int8_t)42;

  // Scalar int8_t load
  // CHECK: rawBufferLoad.i32({{.*}}, i8 1,
  int8_t s = scalarbuf[1];
  // CHECK: trunc i32 {{.*}} to i8
  scalarbuf[2] = s;

  // Struct with i8 first: stores i8 at offset 0, i32 at offset 4
  S1 data1;
  data1.x = 10;
  data1.z = 100;
  // CHECK: rawBufferStore.i8({{.*}}, i32 0, i32 0, i8 10,
  // CHECK: rawBufferStore.i32({{.*}}, i32 0, i32 4, i32 100,
  ubuf1[0] = data1;

  // Load from StructuredBuffer<S1>
  // CHECK: rawBufferLoad.i32
  S1 loaded1 = sbuf1[0];
  ubuf1[1] = loaded1;

  // Struct with i8 last: stores i32 at offset 0, i8 at offset 4
  S2 data2;
  data2.z = 200;
  data2.x = 55;
  // CHECK: rawBufferStore.i32({{.*}}, i32 0, i32 0, i32 200,
  // CHECK: rawBufferStore.i8({{.*}}, i32 0, i32 4, i8 55,
  ubuf2[0] = data2;

  // Load from StructuredBuffer<S2>
  S2 loaded2 = sbuf2[0];
  ubuf2[1] = loaded2;
}

// COPILOT-TODO: We should also test that int8_t/uint8_t fields in structs used
// with structured buffers are correctly aligned and padded according to HLSL
// rules, and that the correct offsets are used for loads/stores. We should test
// various struct layouts (e.g. int8_t followed by int, int followed by int8_t,
// multiple int8_t fields in a row, etc.). We should also test that vector types
// like int8_t4 are stored/loaded using the correct number of bytes and
// alignment. We should also test that int8_t/uint8_t can be used in
// RWByteAddressBuffer and that the correct byte offsets are used for
// loads/stores there as well.

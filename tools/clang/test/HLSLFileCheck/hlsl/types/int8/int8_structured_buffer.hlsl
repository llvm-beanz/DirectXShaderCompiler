// RUN: %dxc -E main -T cs_6_10 %s | FileCheck %s

// Test int8_t/uint8_t in StructuredBuffer and RWStructuredBuffer.
// Verifies that i8 values are loaded/stored using rawBufferLoad/Store.i8.
// Also tests struct layouts, vector types, and RWByteAddressBuffer.

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

// Struct with multiple consecutive int8_t fields.
// In StructuredBuffers each int8_t scalar field is padded to 4 bytes.
// CHECK: ; int8_t a;{{.*}}; Offset:    0
// CHECK: ; int8_t b;{{.*}}; Offset:    4
// CHECK: ; int8_t c;{{.*}}; Offset:    8
// CHECK: ; int d;{{.*}}; Offset:   12
struct S_multi {
  int8_t a;  // offset 0
  int8_t b;  // offset 4
  int8_t c;  // offset 8
  int    d;  // offset 12
};

// Struct with int8_t scalar followed by int8_t4 vector.
// int8_t4 vector is 4-byte aligned in StructuredBuffers.
// CHECK: ; int8_t x;{{.*}}; Offset:    0
// CHECK: ; int8_t4 v;{{.*}}; Offset:    4
struct S_vec {
  int8_t  x;  // offset 0
  int8_t4 v;  // offset 4
};

StructuredBuffer<S1>    sbuf1 : register(t0);
RWStructuredBuffer<S1>  ubuf1 : register(u0);
StructuredBuffer<S2>    sbuf2 : register(t1);
RWStructuredBuffer<S2>  ubuf2 : register(u1);
RWStructuredBuffer<int8_t>  scalarbuf : register(u2);
RWStructuredBuffer<S_multi> smulti : register(u3);
RWStructuredBuffer<S_vec>   svec   : register(u4);
RWByteAddressBuffer         bab    : register(u5);

[numthreads(1, 1, 1)]
void main() {
  // Scalar int8_t store
  // CHECK: rawBufferStore.i8({{.*}}, i32 0, i32 0, i8 42,
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

  // Struct with multiple consecutive int8_t fields: each field is 4-byte
  // aligned in StructuredBuffers, stored at byte offsets 0, 4, 8, 12.
  S_multi m;
  m.a = 1; m.b = 2; m.c = 3; m.d = 100;
  // CHECK: rawBufferStore.i8({{.*}}, i32 0, i32 0, i8 1,
  // CHECK: rawBufferStore.i8({{.*}}, i32 0, i32 4, i8 2,
  // CHECK: rawBufferStore.i8({{.*}}, i32 0, i32 8, i8 3,
  // CHECK: rawBufferStore.i32({{.*}}, i32 0, i32 12, i32 100,
  smulti[0] = m;

  // Struct with int8_t4 vector: vector stored via rawBufferVectorStore.v4i32
  // at element offset 4 (immediately after the preceding int8_t field).
  S_vec sv;
  sv.x = 10;
  sv.v = int8_t4(1, 2, 3, 4);
  // CHECK: rawBufferStore.i8({{.*}}, i32 0, i32 0, i8 10,
  // CHECK: rawBufferVectorStore.v4i32({{.*}}, i32 0, i32 4,
  svec[0] = sv;

  // RWByteAddressBuffer: Store/Load int8_t at explicit byte offsets.
  // Store uses rawBufferStore.i8; load uses rawBufferLoad.i32 then trunc.
  // CHECK: rawBufferStore.i8({{.*}}, i32 0, i32 undef, i8 42,
  bab.Store<int8_t>(0, (int8_t)42);
  // CHECK: rawBufferLoad.i32({{.*}}, i32 1, i32 undef,
  int8_t bv = bab.Load<int8_t>(1);
  // CHECK: rawBufferStore.i8({{.*}}, i32 2, i32 undef,
  bab.Store<int8_t>(2, bv);

  // RWByteAddressBuffer: Store/Load uint8_t.
  // CHECK: rawBufferStore.i8({{.*}}, i32 4, i32 undef,
  bab.Store<uint8_t>(4, (uint8_t)200);
  // CHECK: rawBufferLoad.i32({{.*}}, i32 5, i32 undef,
  uint8_t ubv = bab.Load<uint8_t>(5);
  // CHECK: rawBufferStore.i8({{.*}}, i32 6, i32 undef,
  bab.Store<uint8_t>(6, ubv);
}

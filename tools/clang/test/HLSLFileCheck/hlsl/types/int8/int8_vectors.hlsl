// RUN: %dxc -E main -T cs_6_10 %s | FileCheck %s

// Test int8_t and uint8_t vector types (int8_t2, int8_t4) in structured
// buffers. Because DXIL's rawBufferVectorLoad/Store does not have i8 vector
// overloads, the compiler widens each int8_t element to i32, so int8_t2 uses
// the v2i32 overload (stride=8) and uint8_t4 uses the v4i32 overload
// (stride=16).

// Verify that the buffers are annotated with the correct element strides:
// int8_t2 elements are widened to <2 x i32> => stride=8 bytes.
// CHECK: RWStructuredBuffer<stride=8>
// uint8_t4 elements are widened to <4 x i32> => stride=16 bytes.
// CHECK: RWStructuredBuffer<stride=16>

RWStructuredBuffer<int8_t2> v2buf : register(u0);
RWStructuredBuffer<uint8_t4> v4buf : register(u1);

[numthreads(1, 1, 1)]
void main() {
  // Load int8_t2 as a <2 x i32> vector (elements widened from i8 to i32).
  // CHECK: rawBufferVectorLoad.v2i32
  int8_t2 v2 = v2buf[0];

  // Store int8_t2 back as a <2 x i32> vector.
  // CHECK: rawBufferVectorStore.v2i32
  v2buf[1] = v2;

  // Load uint8_t4 as a <4 x i32> vector (elements widened from i8 to i32).
  // CHECK: rawBufferVectorLoad.v4i32
  uint8_t4 v4 = v4buf[0];

  // Store uint8_t4 back as a <4 x i32> vector.
  // CHECK: rawBufferVectorStore.v4i32
  v4buf[1] = v4;
}

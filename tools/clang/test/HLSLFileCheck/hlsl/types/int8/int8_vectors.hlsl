// RUN: %dxc -E main -T cs_6_10 %s | FileCheck %s

// Test int8_t and uint8_t vector types (int8_t2, int8_t4) in structured
// buffers. DXIL's rawBufferVectorLoad/Store has native i8 vector overloads,
// so int8_t2 uses the v2i8 overload (stride=8) and uint8_t4 uses the v4i8
// overload (stride=16). The element stride reflects the HLSL host layout where
// each int8_t occupies 4 bytes (i8:32 ABI).

// Verify that the buffers are annotated with the correct element strides:
// int8_t2 host layout is <2 x i32> => stride=8 bytes.
// CHECK: RWStructuredBuffer<stride=8>
// uint8_t4 host layout is <4 x i32> => stride=16 bytes.
// CHECK: RWStructuredBuffer<stride=16>

RWStructuredBuffer<int8_t2> v2buf : register(u0);
RWStructuredBuffer<uint8_t4> v4buf : register(u1);

[numthreads(1, 1, 1)]
void main() {
  // Load int8_t2 as a native <2 x i8> vector.
  // CHECK: rawBufferVectorLoad.v2i8
  int8_t2 v2 = v2buf[0];

  // Store int8_t2 back as a native <2 x i8> vector.
  // CHECK: rawBufferVectorStore.v2i8
  v2buf[1] = v2;

  // Load uint8_t4 as a native <4 x i8> vector.
  // CHECK: rawBufferVectorLoad.v4i8
  uint8_t4 v4 = v4buf[0];

  // Store uint8_t4 back as a native <4 x i8> vector.
  // CHECK: rawBufferVectorStore.v4i8
  v4buf[1] = v4;
}

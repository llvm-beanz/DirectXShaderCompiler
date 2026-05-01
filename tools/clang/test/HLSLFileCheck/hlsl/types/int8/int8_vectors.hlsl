// RUN: %dxc -E main -T cs_6_10 %s | FileCheck %s

// Test int8_t and uint8_t vector types (int8_t2, int8_t3, int8_t4,
// uint8_t2, uint8_t3, uint8_t4) in structured buffers.

RWStructuredBuffer<int8_t2> v2buf : register(u0);
RWStructuredBuffer<uint8_t4> v4buf : register(u1);

[numthreads(1, 1, 1)]
void main() {
  // Load and store int8_t2 vector
  // CHECK: rawBufferVectorLoad.v2i32
  int8_t2 v2 = v2buf[0];

  // CHECK: rawBufferVectorStore.v2i32
  v2buf[1] = v2;

  // Load and store uint8_t4 vector
  // CHECK: rawBufferVectorLoad.v4i32
  uint8_t4 v4 = v4buf[0];

  // CHECK: rawBufferVectorStore.v4i32
  v4buf[1] = v4;
}

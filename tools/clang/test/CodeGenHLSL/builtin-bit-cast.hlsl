// RUN: %dxc -Tcs_6_0 -E main %s | FileCheck %s

// Verify that __builtin_bit_cast produces bit-preserving conversions for
// scalar and vector types.

RWStructuredBuffer<uint> Out;
RWStructuredBuffer<float> InFloat;

[numthreads(1,1,1)]
void main(uint3 tid : SV_DispatchThreadID) {
  float f = InFloat[tid.x];
  // CHECK: bitcast float {{.*}} to i32
  Out[0] = __builtin_bit_cast(uint, f);

  float2 v = float2(InFloat[0], InFloat[1]);
  // CHECK: bitcast float {{.*}} to i32
  // CHECK: bitcast float {{.*}} to i32
  uint2 vu = __builtin_bit_cast(uint2, v);
  Out[1] = vu.x + vu.y;
}

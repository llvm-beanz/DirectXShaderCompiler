// RUN: %dxc -Tcs_6_0 -E main %s | FileCheck %s

// Verify that the <bit_cast> built-in header provides hlsl::bit_cast<>.

#include <bit_cast.h>

RWStructuredBuffer<uint> Out;
RWStructuredBuffer<float> In;

[numthreads(1,1,1)]
void main(uint3 tid : SV_DispatchThreadID) {
  float f = In[tid.x];
  // CHECK: bitcast float {{.*}} to i32
  Out[0] = hlsl::bit_cast<uint>(f);
}

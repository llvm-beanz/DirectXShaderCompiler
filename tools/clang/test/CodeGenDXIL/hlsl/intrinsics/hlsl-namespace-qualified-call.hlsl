// RUN: %dxc -T cs_6_6 -HV 202x -E main %s | FileCheck %s

// Verify that under HLSL 202x, after an unqualified use of an intrinsic
// causes it to be added to the 'hlsl' namespace, the same intrinsic can be
// invoked through an explicit 'hlsl::' qualifier.  Both qualified and
// unqualified forms must compile and produce calls to the same intrinsic.

RWBuffer<float> Out;

[numthreads(1,1,1)]
void main(uint tid : SV_DispatchThreadID) {
  float x = (float)tid;
  float y = (float)(tid + 1);
  // Unqualified call materializes the intrinsic in the 'hlsl' namespace.
  float u = sin(x);
  // Qualified call must resolve to the same intrinsic via the namespace.
  float q = hlsl::sin(y);
  Out[tid] = u + q;
}

// Both calls should lower to dx.op.unary Sin (opcode 13).
// CHECK: call float @dx.op.unary.f32(i32 13, float
// CHECK: call float @dx.op.unary.f32(i32 13, float

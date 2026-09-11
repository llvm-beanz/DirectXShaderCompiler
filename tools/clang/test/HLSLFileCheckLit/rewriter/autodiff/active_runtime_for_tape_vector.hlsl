// RUN: %dxr -generate-differentials %s | FileCheck %s
// RUN: %dxr -generate-differentials %s > %t.gen.hlsl
// RUN: cat %t.gen.hlsl %S/Inputs/active_runtime_for_tape_vector_caller.hlsl > %t.run.hlsl
// RUN: %dxc -T cs_6_9 -E testMain -HV 2021 -Fo %t.dxil %t.run.hlsl
// RUN: %dxc -dumpbin %t.dxil | FileCheck %s --check-prefix=EXEC

// Vector self-products tape and reverse component-wise.

// CHECK: float2 __dxc_ad_loop_0_primal_tape[4];
// CHECK: __dxc_ad_loop_0_adjoint *= (2 * __dxc_ad_loop_0_primal_tape[iteration]);

// One iteration maps (2,3) to (4,9). Seed (2,3) times derivative (4,6)
// produces gradient (8,18).
// EXEC: rawBufferStore.f32{{.*}}i32 0, i32 0, float 4.000000e+00
// EXEC: rawBufferStore.f32{{.*}}i32 1, i32 0, float 9.000000e+00
// EXEC: rawBufferStore.f32{{.*}}i32 2, i32 0, float 8.000000e+00
// EXEC: rawBufferStore.f32{{.*}}i32 3, i32 0, float 1.800000e+01

[[dxc::autodiff(bwd)]]
float2 f(float2 value, [[dxc::no_diff]] uint count) {
  for (uint iteration = 0; iteration < min(count, 4); ++iteration)
    value *= value;
  return value;
}
// RUN: %dxr -generate-differentials %s | FileCheck %s
// RUN: %dxr -generate-differentials %s > %t.gen.hlsl
// RUN: cat %t.gen.hlsl %S/Inputs/active_runtime_for_coupled_affine_caller.hlsl > %t.run.hlsl
// RUN: %dxc -T cs_6_9 -E testMain -HV 2021 -Fo %t.dxil %t.run.hlsl
// RUN: %dxc -dumpbin %t.dxil | FileCheck %s --check-prefix=EXEC

// Assignment-form coupled products preserve the inactive affine term in the
// primal while using the same taped Jacobian as x *= y.

// CHECK: x.value = ((x.value * y.value) + bias);
// CHECK: y.value *= factor;
// CHECK: __dxc_ad_loop_0_secondary_adjoint += __dxc_ad_loop_0_primal_tape[iteration] * __dxc_ad_loop_0_primary_adjoint;
// CHECK: __dxc_ad_loop_0_primary_adjoint *= __dxc_ad_loop_1_primal_tape[iteration];

// Starting from (2,3), bias=1, and factor=2 gives (7,6) then (43,12),
// returning 55. Seed 2 produces gradients (36,60).
// EXEC: rawBufferStore.f32{{.*}}i32 0, i32 0, float 5.500000e+01
// EXEC: rawBufferStore.f32{{.*}}i32 1, i32 0, float 3.600000e+01
// EXEC: rawBufferStore.f32{{.*}}i32 2, i32 0, float 6.000000e+01

[[dxc::autodiff(bwd)]]
float f(float x, float y, [[dxc::no_diff]] uint count,
        [[dxc::no_diff]] float bias, [[dxc::no_diff]] float factor) {
  for (uint iteration = 0; iteration < min(count, 8); ++iteration) {
    x = x * y + bias;
    y *= factor;
  }
  return x + y;
}

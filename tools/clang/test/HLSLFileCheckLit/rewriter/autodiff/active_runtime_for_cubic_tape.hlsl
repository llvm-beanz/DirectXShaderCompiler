// RUN: %dxr -generate-differentials %s | FileCheck %s
// RUN: %dxr -generate-differentials %s > %t.gen.hlsl
// RUN: cat %t.gen.hlsl %S/Inputs/active_runtime_for_cubic_tape_caller.hlsl > %t.run.hlsl
// RUN: %dxc -T cs_6_9 -E testMain -HV 2021 -Fo %t.dxil %t.run.hlsl
// RUN: %dxc -dumpbin %t.dxil | FileCheck %s --check-prefix=EXEC

// Cubic recurrences reconstruct 3*x*x plus the inactive linear coefficient.

// CHECK: value.value = (((value.value * value.value) * value.value) + (slope * value.value));
// CHECK: __dxc_ad_loop_0_adjoint *= (3 * __dxc_ad_loop_0_primal_tape[iteration] * __dxc_ad_loop_0_primal_tape[iteration] + slope);

// Starting at 1 with slope 1 gives 2 then 10. Local derivatives are 4 and 13,
// so seed 2 produces gradient 104.
// EXEC: rawBufferStore.f32{{.*}}i32 0, i32 0, float 1.000000e+01
// EXEC: rawBufferStore.f32{{.*}}i32 1, i32 0, float 1.040000e+02

[[dxc::autodiff(bwd)]]
float f(float value, [[dxc::no_diff]] uint count,
        [[dxc::no_diff]] float slope) {
  for (uint iteration = 0; iteration < min(count, 8); ++iteration)
    value = value * value * value + slope * value;
  return value;
}

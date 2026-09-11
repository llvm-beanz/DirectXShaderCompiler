// RUN: %dxr -generate-differentials %s | FileCheck %s
// RUN: %dxr -generate-differentials %s > %t.gen.hlsl
// RUN: cat %t.gen.hlsl %S/Inputs/active_runtime_for_polynomial_tape_caller.hlsl > %t.run.hlsl
// RUN: %dxc -T cs_6_9 -E testMain -HV 2021 -Fo %t.dxil %t.run.hlsl
// RUN: %dxc -dumpbin %t.dxil | FileCheck %s --check-prefix=EXEC

// A bounded quadratic recurrence tapes its carried primal and recomputes the
// local derivative 2*x + scale at each reverse step.

// CHECK: value.value = (((value.value * value.value) + (scale * value.value)) + bias);
// CHECK: __dxc_ad_loop_0_adjoint *= (2 * __dxc_ad_loop_0_primal_tape[iteration] + scale);

// Starting at 1 with scale 2 and bias 1 gives 4 then 25. Local derivatives
// are 4 and 10, so seed 3 produces gradient 120.
// EXEC: rawBufferStore.f32{{.*}}i32 0, i32 0, float 2.500000e+01
// EXEC: rawBufferStore.f32{{.*}}i32 1, i32 0, float 1.200000e+02

[[dxc::autodiff(bwd)]]
float f(float value, [[dxc::no_diff]] uint count,
        [[dxc::no_diff]] float scale, [[dxc::no_diff]] float bias) {
  for (uint iteration = 0; iteration < min(count, 8); ++iteration)
    value = value * value + scale * value + bias;
  return value;
}

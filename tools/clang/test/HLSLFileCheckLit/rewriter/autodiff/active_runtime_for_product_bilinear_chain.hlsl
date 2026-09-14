// RUN: %dxr -generate-differentials %s | FileCheck %s
// RUN: %dxr -generate-differentials %s > %t.gen.hlsl
// RUN: cat %t.gen.hlsl %S/Inputs/active_runtime_for_product_bilinear_chain_caller.hlsl > %t.run.hlsl
// RUN: %dxc -T cs_6_9 -E testMain -HV 2021 -Fo %t.dxil %t.run.hlsl
// RUN: %dxc -dumpbin %t.dxil | FileCheck %s --check-prefix=EXEC

// Independent target and peer linear terms contribute to their corresponding
// partial derivatives around a scaled product edge.

// CHECK: x.value = (((((scale * x.value) * y.value) + (targetSlope * x.value)) + (peerSlope * y.value)) + bias);
// CHECK: __dxc_ad_loop_0_state_1_adjoint += (scale * __dxc_ad_loop_0_primal_tape[iteration] + peerSlope) * __dxc_ad_loop_0_state_0_adjoint;
// CHECK: __dxc_ad_loop_0_state_0_adjoint *= (scale * __dxc_ad_loop_1_primal_tape[iteration] + targetSlope);

// Starting at (2,3,4), two iterations produce (25.75,15,16), returning 56.75.
// With seed 2, reverse Jacobian replay produces (16,18.25,20.25).
// EXEC: rawBufferStore.f32{{.*}}i32 0, i32 0, float 5.675000e+01
// EXEC: rawBufferStore.f32{{.*}}i32 1, i32 0, float 1.600000e+01
// EXEC: rawBufferStore.f32{{.*}}i32 2, i32 0, float 1.825000e+01
// EXEC: rawBufferStore.f32{{.*}}i32 3, i32 0, float 2.025000e+01

[[dxc::autodiff(bwd)]]
float f(float x, float y, float z, [[dxc::no_diff]] uint count,
        [[dxc::no_diff]] float scale, [[dxc::no_diff]] float targetSlope,
        [[dxc::no_diff]] float peerSlope, [[dxc::no_diff]] float bias,
        [[dxc::no_diff]] float factor) {
  for (uint iteration = 0; iteration < min(count, 8); ++iteration) {
    x = scale * x * y + targetSlope * x + peerSlope * y + bias;
    y += z;
    z *= factor;
  }
  return x + y + z;
}

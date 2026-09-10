// RUN: %dxr -generate-differentials %s | FileCheck %s
//
// Compile the rewritten output and confirm that the generated _Static_assert
// fires. The fallback return also diagnoses its empty initializer.
//
// RUN: %dxr -generate-differentials %s > %t.gen.hlsl
// RUN: not %dxc -T ps_6_9 -HV 2021 %t.gen.hlsl 2>&1 | FileCheck %s --check-prefix=DIAG

// DIAG: error: static_assert failed "auto-diff cannot generate backward-mode for 'use_cmp': the ternary ?: operator is not differentiable"
// DIAG: error: 'Variable<float>' cannot have an explicit empty initializer

// Comparison operators are not differentiable; the function gets a stub.

// CHECK: namespace user { namespace ad { namespace bwd {
// CHECK: Variable<float> use_cmp(inout GradientContext<float> context, Variable<float> x, Variable<float> y)
// CHECK: _Static_assert(false, "auto-diff cannot generate backward-mode for 'use_cmp': the ternary ?: operator is not differentiable");
// CHECK: } } } // namespace user::ad::bwd

[[dxc::autodiff(bwd)]]
float use_cmp(float x, float y) {
  return (x < y) ? x : y;
}

float main(float x : A) : SV_Target { return use_cmp(x, x); }

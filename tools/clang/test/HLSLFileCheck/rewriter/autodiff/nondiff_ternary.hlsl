// RUN: %dxr -generate-differentials %s | FileCheck %s

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

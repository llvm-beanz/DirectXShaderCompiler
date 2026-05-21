// RUN: %dxr -generate-differentials %s | FileCheck %s

// Forward-mode counterpart to nondiff_ternary.hlsl: the ternary ?: is not
// differentiable, so the forward-mode generated function is replaced by a
// _Static_assert stub with a human-readable reason.

// CHECK: namespace user { namespace ad { namespace fwd {
// CHECK: Value<float> use_cmp(Value<float> x, Value<float> y)
// CHECK: _Static_assert(false, "auto-diff cannot generate forward-mode for 'use_cmp': the ternary ?: operator is not differentiable");
// CHECK: } } } // namespace user::ad::fwd

[[dxc::autodiff(fwd)]]
float use_cmp(float x, float y) {
  return (x < y) ? x : y;
}

float main(float x : A) : SV_Target { return use_cmp(x, x); }

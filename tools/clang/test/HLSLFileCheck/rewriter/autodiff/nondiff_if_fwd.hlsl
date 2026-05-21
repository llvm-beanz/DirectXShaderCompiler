// RUN: %dxr -generate-differentials %s | FileCheck %s

// Forward-mode counterpart to nondiff_if.hlsl: data-dependent control flow
// (if) is not soundly differentiable; the forward-mode function body is
// replaced by a _Static_assert stub pointing the user at [[dxc::no_diff]] or
// branchless math.

// CHECK: namespace user { namespace ad { namespace fwd {
// CHECK: Value<float> branchy(Value<float> x)
// CHECK: _Static_assert(false, "auto-diff cannot generate forward-mode for 'branchy': data-dependent control flow (if) is not differentiable; use {{\[\[}}dxc::no_diff]] or branchless math");
// CHECK: } } } // namespace user::ad::fwd

[[dxc::autodiff(fwd)]]
float branchy(float x) {
  if (x > 0)
    return x;
  return -x;
}

float main(float x : A) : SV_Target { return branchy(x); }

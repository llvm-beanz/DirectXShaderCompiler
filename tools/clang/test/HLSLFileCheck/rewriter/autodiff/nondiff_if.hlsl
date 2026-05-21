// RUN: %dxr -generate-differentials %s | FileCheck %s

// Data-dependent control flow (if) is not soundly differentiable; the
// function body is replaced by a _Static_assert with a message pointing
// the user at [[no_diff]] / branchless math.

// CHECK: namespace user { namespace ad { namespace bwd {
// CHECK: Variable<float> branchy(inout GradientContext<float> context, Variable<float> x)
// CHECK: _Static_assert(false, "auto-diff cannot generate backward-mode for 'branchy': data-dependent control flow (if) is not differentiable; use {{\[\[}}dxc::no_diff]] or branchless math");
// CHECK: } } } // namespace user::ad::bwd

[[dxc::autodiff(bwd)]]
float branchy(float x) {
  if (x > 0)
    return x;
  return -x;
}

float main(float x : A) : SV_Target { return branchy(x); }

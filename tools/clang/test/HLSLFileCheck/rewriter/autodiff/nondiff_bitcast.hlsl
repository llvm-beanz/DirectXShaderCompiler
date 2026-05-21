// RUN: %dxr -generate-differentials %s | FileCheck %s

// Non-differentiable intrinsics generate a _Static_assert stub with a
// human-readable reason rather than miscompiling silently.

// CHECK: namespace user { namespace ad { namespace fwd {
// CHECK: Value<float> uses_asint(Value<float> x)
// CHECK: _Static_assert(false, "auto-diff cannot generate forward-mode for 'uses_asint': bit-cast 'asint' is not differentiable");
// CHECK: } } } // namespace user::ad::fwd

// CHECK: namespace user { namespace ad { namespace bwd {
// CHECK: Variable<float> uses_asint(inout GradientContext<float> context, Variable<float> x)
// CHECK: _Static_assert(false, "auto-diff cannot generate backward-mode for 'uses_asint': bit-cast 'asint' is not differentiable");
// CHECK: } } } // namespace user::ad::bwd

[[dxc::autodiff(fwd, bwd)]]
float uses_asint(float x) { return asint(x); }

float main(float x : A) : SV_Target { return uses_asint(x); }

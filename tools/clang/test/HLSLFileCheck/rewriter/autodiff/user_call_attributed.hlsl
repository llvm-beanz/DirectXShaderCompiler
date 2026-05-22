// RUN: %dxr -generate-differentials %s | FileCheck %s

// Both `f` and the user function `g` it calls are annotated with
// [[dxc::autodiff(fwd, bwd)]]. In forward mode, the generated
// `user::ad::fwd::f` invokes `g` unqualified; C++ unqualified name lookup
// resolves it to `user::ad::fwd::g`, which the rewriter also emits, so the
// forward variant composes through the user call correctly.
//
// In backward mode the rewriter currently does NOT recognise an attributed
// user callee as a candidate for the `g(context, x_expr)` rewriting it
// applies to intrinsics. Instead, `g` falls through the
// GetBackwardIntrinsicBuilder lookup and produces a non-differentiable
// stub. This test records that current behaviour so that any future fix
// (which should emit a real call into `user::ad::bwd::g`) updates the
// expectation deliberately.

// CHECK: namespace user { namespace ad { namespace fwd {
// CHECK: Value<float> g(Value<float> x)
// CHECK: return (x * x);
// CHECK: } } } // namespace user::ad::fwd

// CHECK: namespace user { namespace ad { namespace bwd {
// CHECK: Variable<float> g(inout GradientContext<float> context, Variable<float> x)
// CHECK: return multiply<float>(x_expr, x_expr);
// CHECK: } } } // namespace user::ad::bwd

// CHECK: namespace user { namespace ad { namespace fwd {
// CHECK: Value<float> f(Value<float> x)
// CHECK: return (g(x) + x);
// CHECK: } } } // namespace user::ad::fwd

// CHECK: namespace user { namespace ad { namespace bwd {
// CHECK: Variable<float> f(inout GradientContext<float> context, Variable<float> x)
// CHECK: _Static_assert(false, "auto-diff cannot generate backward-mode for 'f': unknown callee 'g' has no auto-diff builder");
// CHECK: return Variable<float>();
// CHECK: } } } // namespace user::ad::bwd

[[dxc::autodiff(fwd, bwd)]]
float g(float x) { return x * x; }

[[dxc::autodiff(fwd, bwd)]]
float f(float x) { return g(x) + x; }

float main(float x : A) : SV_Target { return f(x); }

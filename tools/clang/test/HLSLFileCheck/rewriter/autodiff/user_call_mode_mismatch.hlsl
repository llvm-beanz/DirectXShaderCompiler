// RUN: %dxr -generate-differentials %s | FileCheck %s

// `f` is annotated with both forward and backward auto-diff modes but `g`
// is annotated with forward mode only. Forward-mode of `f` therefore has a
// matching `user::ad::fwd::g` to dispatch to via unqualified name lookup,
// while backward-mode of `f` still falls into the unknown-callee branch
// because `g` has no backward overload and is not an intrinsic.
//
// The test pins down current behaviour: the rewriter does not inspect the
// callee's [[dxc::autodiff]] modes when deciding whether a call site is
// differentiable in backward mode. A future improvement could promote
// mode-mismatched callees to a clearer diagnostic; this expectation should
// then be updated to match.

// CHECK: namespace user { namespace ad { namespace fwd {
// CHECK: Value<float> g(Value<float> x)
// CHECK: return (x * x);
// CHECK: } } } // namespace user::ad::fwd

// The user does not provide a backward variant of g, and the rewriter
// must not invent one.
// CHECK-NOT: Variable<float> g(

// CHECK: namespace user { namespace ad { namespace fwd {
// CHECK: Value<float> f(Value<float> x)
// CHECK: return (g(x) + x);
// CHECK: } } } // namespace user::ad::fwd

// CHECK: namespace user { namespace ad { namespace bwd {
// CHECK: Variable<float> f(inout GradientContext<float> context, Variable<float> x)
// CHECK: _Static_assert(false, "auto-diff cannot generate backward-mode for 'f': unknown callee 'g' has no auto-diff builder");
// CHECK: return Variable<float>();
// CHECK: } } } // namespace user::ad::bwd

[[dxc::autodiff(fwd)]]
float g(float x) { return x * x; }

[[dxc::autodiff(fwd, bwd)]]
float f(float x) { return g(x) + x; }

float main(float x : A) : SV_Target { return f(x); }

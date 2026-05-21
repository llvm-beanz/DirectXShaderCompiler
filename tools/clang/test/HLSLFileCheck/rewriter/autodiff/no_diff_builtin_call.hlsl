// RUN: %dxr -generate-differentials %s | FileCheck %s
//
// Note: this test intentionally does not run a `dxc -verify` pass on the
// rewriter output. The rewriter preserves `[[dxc::no_diff]]` substatements
// (and forward-mode compound assignment) verbatim, but the surrounding
// generated function rebinds parameter types to `Value<T>` / `Variable<T>`,
// so the preserved code mixes scalar `float` operations with user-type
// values and fails to type-check. This is a pre-existing rewriter
// limitation documented in agent_thoughts.md and is out of scope for the
// verify-coverage change.

// [[dxc::no_diff]] applied to a statement containing a call to a builtin HLSL
// intrinsic. The call is copied verbatim instead of being mapped to the
// backward-mode '*Expr' builder; this lets a user opt out of differentiating
// a known intrinsic on a per-statement basis.

// CHECK: namespace user { namespace ad { namespace fwd {
// CHECK: Value<float> f(Value<float> x)
// CHECK: Value<float> a;
// CHECK: a = sin(x);
// CHECK: return (a + x);
// CHECK: } } } // namespace user::ad::fwd
// CHECK: namespace user { namespace ad { namespace bwd {
// CHECK: Variable<float> f(inout GradientContext<float> context, Variable<float> x)
// CHECK: VariableExpr<float> x_expr = makeVariableExpr<float>(x);
// CHECK: Variable<float> a;
// CHECK: a = sin(x);
// CHECK: return add<float>(a, x_expr);
// CHECK: } } } // namespace user::ad::bwd

[[dxc::autodiff(fwd, bwd)]]
float f(float x) {
  float a;
  [[dxc::no_diff]] a = sin(x);
  return a + x;
}

float main(float x : A) : SV_Target { return f(x); }

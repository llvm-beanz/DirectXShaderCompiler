// RUN: %dxr -generate-differentials %s | FileCheck %s

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

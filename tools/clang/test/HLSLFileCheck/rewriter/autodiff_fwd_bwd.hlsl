// RUN: %dxr -generate-differentials %s | FileCheck %s

// Combined forward and backward autodiff: both variants are emitted in
// their respective namespaces, and the unary math intrinsics sin/cos/exp
// are translated correctly.

// CHECK: namespace user { namespace ad { namespace fwd {
// CHECK: Value<float> f(Value<float> x)
// CHECK: return ((sin(x) * cos(x)) + exp(x));
// CHECK: } } } // namespace user::ad::fwd
// CHECK: namespace user { namespace ad { namespace bwd {
// CHECK: Variable<float> f(inout GradientContext<float> context, Variable<float> x)
// CHECK: VariableExpr<float> x_expr = makeVariableExpr<float>(x);
// CHECK: return add<float>(multiply<float>(sinExpr<float>(x_expr), cosExpr<float>(x_expr)), expExpr<float>(x_expr));
// CHECK: } } } // namespace user::ad::bwd

[[dxc::autodiff(fwd, bwd)]]
float f(float x) {
  return sin(x) * cos(x) + exp(x);
}

float main(float x : A) : SV_Target { return f(x); }

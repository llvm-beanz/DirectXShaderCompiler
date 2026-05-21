// RUN: %dxr -generate-differentials %s | FileCheck %s

// Local variable declarations inside the differentiated function body are
// translated through the expression rewriter and surfaced as Variable<T>
// declarations in backward mode (Value<T> in forward).

// CHECK: namespace user { namespace ad { namespace fwd {
// CHECK: Value<float> f(Value<float> x, Value<float> y)
// CHECK: Value<float> a = (x * y);
// CHECK: Value<float> b = (a + sin(x));
// CHECK: return b;
// CHECK: } } } // namespace user::ad::fwd
// CHECK: namespace user { namespace ad { namespace bwd {
// CHECK: Variable<float> f(inout GradientContext<float> context, Variable<float> x, Variable<float> y)
// CHECK: VariableExpr<float> x_expr = makeVariableExpr<float>(x);
// CHECK: VariableExpr<float> y_expr = makeVariableExpr<float>(y);
// CHECK: Variable<float> a = multiply<float>(x_expr, y_expr);
// CHECK: Variable<float> b = add<float>(a, sinExpr<float>(x_expr));
// CHECK: return b;
// CHECK: } } } // namespace user::ad::bwd

[[dxc::autodiff(fwd, bwd)]]
float f(float x, float y) {
  float a = x * y;
  float b = a + sin(x);
  return b;
}

float main(float x : A) : SV_Target { return f(x, x); }

// RUN: %dxr -generate-differentials %s | FileCheck %s

// The [[dxc::no_diff]] statement attribute marks a statement that should NOT be
// translated by the auto-diff rewriter; the wrapped statement is copied
// verbatim into the generated function. The attribute can be applied to any
// statement that wraps into an AttributedStmt -- typically a compound block,
// an expression statement, or a return statement.

// CHECK: namespace user { namespace ad { namespace fwd {
// CHECK: Value<float> f(Value<float> x)
// CHECK: return x * x + x;
// CHECK: } } } // namespace user::ad::fwd
// CHECK: namespace user { namespace ad { namespace bwd {
// CHECK: Variable<float> f(inout GradientContext<float> context, Variable<float> x)
// CHECK: VariableExpr<float> x_expr = makeVariableExpr<float>(x);
// CHECK: return x * x + x;
// CHECK: } } } // namespace user::ad::bwd

[[dxc::autodiff(fwd, bwd)]]
float f(float x) {
  [[dxc::no_diff]] return x * x + x;
}

float main(float x : A) : SV_Target { return f(x); }

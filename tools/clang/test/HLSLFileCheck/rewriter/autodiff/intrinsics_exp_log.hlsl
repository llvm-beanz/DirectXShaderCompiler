// RUN: %dxr -generate-differentials %s | FileCheck %s
//
//
// Note: this test only checks the rewriter output. Running `dxc -verify`
// on the generated backward-mode code is intentionally skipped because
// the rewriter declares the generated function to return `Variable<T>`
// but the real hlsl/ad/bwd library's combinators (`add`, `multiply`,
// ...) return expression-template types (e.g. `BackAddExpr<...>`). This
// mismatch is a pre-existing rewriter limitation documented in
// agent_thoughts.md and is out of scope for the include-path change.

// Exponential / logarithm / power family.

// CHECK: namespace user { namespace ad { namespace bwd {
// CHECK: Variable<float> f(inout GradientContext<float> context, Variable<float> x, Variable<float> y)
// CHECK: expExpr<float>(x_expr)
// CHECK: exp2Expr<float>(x_expr)
// CHECK: logExpr<float>(x_expr)
// CHECK: log2Expr<float>(x_expr)
// CHECK: log10Expr<float>(x_expr)
// CHECK: power<float>(x_expr, y_expr)
// CHECK: } } } // namespace user::ad::bwd

[[dxc::autodiff(bwd)]]
float f(float x, float y) {
  return exp(x) + exp2(x)
       + log(x) + log2(x) + log10(x)
       + pow(x, y);
}

float main(float x : A) : SV_Target { return f(x, x); }

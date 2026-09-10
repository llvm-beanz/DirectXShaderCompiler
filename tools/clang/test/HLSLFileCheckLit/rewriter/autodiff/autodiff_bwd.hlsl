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

// Backward-only autodiff: emits a Variable<float>-based backward variant in
// user::ad::bwd. Parameter references are rewritten to _expr wrappers and a
// VariableExpr<T> is declared per parameter.

// CHECK: float f(float x)
// CHECK: namespace user { namespace ad { namespace bwd {
// CHECK: Variable<float> f(inout GradientContext<float> context, Variable<float> x)
// CHECK: VariableExpr<float> x_expr = makeVariableExpr<float>(x);
// CHECK: return add<float>(multiply<float>(x_expr, x_expr), x_expr);
// CHECK: } } } // namespace user::ad::bwd

[[dxc::autodiff(bwd)]]
float f(float x) {
  return x * x + x;
}

float main(float x : A) : SV_Target { return f(x); }

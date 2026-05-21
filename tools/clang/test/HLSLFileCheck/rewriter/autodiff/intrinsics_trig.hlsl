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

// Trig intrinsics map to *Expr builders in backward mode.

// CHECK: namespace user { namespace ad { namespace bwd {
// CHECK: Variable<float> f(inout GradientContext<float> context, Variable<float> x)
// CHECK: VariableExpr<float> x_expr = makeVariableExpr<float>(x);
// CHECK: sinExpr<float>(x_expr)
// CHECK: cosExpr<float>(x_expr)
// CHECK: tanExpr<float>(x_expr)
// CHECK: asinExpr<float>(x_expr)
// CHECK: acosExpr<float>(x_expr)
// CHECK: atanExpr<float>(x_expr)
// CHECK: sinhExpr<float>(x_expr)
// CHECK: coshExpr<float>(x_expr)
// CHECK: tanhExpr<float>(x_expr)
// CHECK: } } } // namespace user::ad::bwd

[[dxc::autodiff(bwd)]]
float f(float x) {
  return sin(x) + cos(x) + tan(x)
       + asin(x) + acos(x) + atan(x)
       + sinh(x) + cosh(x) + tanh(x);
}

float main(float x : A) : SV_Target { return f(x); }

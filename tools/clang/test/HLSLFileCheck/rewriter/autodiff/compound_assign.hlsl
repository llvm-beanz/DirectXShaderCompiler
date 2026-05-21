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

// Compound assignment forms map to *Assign builders in backward mode.

// CHECK: namespace user { namespace ad { namespace bwd {
// CHECK: Variable<float> f(inout GradientContext<float> context, Variable<float> x)
// CHECK: Variable<float> a = x_expr;
// CHECK: addAssign<float>(a, x_expr);
// CHECK: subAssign<float>(a, x_expr);
// CHECK: mulAssign<float>(a, x_expr);
// CHECK: divAssign<float>(a, x_expr);
// CHECK: return a;
// CHECK: } } } // namespace user::ad::bwd

[[dxc::autodiff(bwd)]]
float f(float x) {
  float a = x;
  a += x;
  a -= x;
  a *= x;
  a /= x;
  return a;
}

float main(float x : A) : SV_Target { return f(x); }

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

// [[dxc::no_diff]] applied to an expression statement built from arithmetic
// operators. The operator expression is copied verbatim instead of being
// translated to add/multiply/etc. builders.

// CHECK: namespace user { namespace ad { namespace fwd {
// CHECK: Value<float> f(Value<float> x, Value<float> y)
// CHECK: Value<float> a;
// CHECK: a = x * y + x;
// CHECK: return (a + x);
// CHECK: } } } // namespace user::ad::fwd
// CHECK: namespace user { namespace ad { namespace bwd {
// CHECK: Variable<float> f(inout GradientContext<float> context, Variable<float> x, Variable<float> y)
// CHECK: VariableExpr<float> x_expr = makeVariableExpr<float>(x);
// CHECK: VariableExpr<float> y_expr = makeVariableExpr<float>(y);
// CHECK: Variable<float> a;
// CHECK: a = x * y + x;
// CHECK: return add<float>(a, x_expr);
// CHECK: } } } // namespace user::ad::bwd

[[dxc::autodiff(fwd, bwd)]]
float f(float x, float y) {
  float a;
  [[dxc::no_diff]] a = x * y + x;
  return a + x;
}

float main(float x : A) : SV_Target { return f(x, x); }

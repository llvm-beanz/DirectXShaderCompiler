// RUN: %dxr -generate-differentials %s | FileCheck %s
//
// Note: this test intentionally does not run a `dxc -verify` pass on the
// rewriter output. The rewriter preserves the `[[dxc::no_diff]]`
// sub-expression verbatim, but the surrounding generated function rebinds
// parameter types to `Value<T>` / `Variable<T>`, so the preserved code
// mixes scalar `float` operations with user-type values and fails to
// type-check. This is the same pre-existing rewriter limitation as for
// the existing statement-level `no_diff_*` tests; see agent_thoughts.md.

// [[dxc::no_diff]] applied to a sub-expression inside a larger expression.
// Only the marked sub-expression (the floor() call) should be copied
// verbatim; the surrounding subtraction must still be rewritten to the
// backward-mode `subtract<float>` builder.

// CHECK: namespace user { namespace ad { namespace fwd {
// CHECK: Value<float> frac_no_diff(Value<float> uv)
// CHECK: return (uv - floor(uv));
// CHECK: } } } // namespace user::ad::fwd
// CHECK: namespace user { namespace ad { namespace bwd {
// CHECK: Variable<float> frac_no_diff(inout GradientContext<float> context, Variable<float> uv)
// CHECK: VariableExpr<float> uv_expr = makeVariableExpr<float>(uv);
// CHECK: return subtract<float>(uv_expr, floor(uv));
// CHECK: } } } // namespace user::ad::bwd

[[dxc::autodiff(fwd, bwd)]]
float frac_no_diff(float uv) {
  return uv - [[dxc::no_diff]] floor(uv);
}

float main(float uv : A) : SV_Target { return frac_no_diff(uv); }

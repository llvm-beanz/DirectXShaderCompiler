// RUN: %dxr -generate-differentials %s | FileCheck %s
//
// [[dxc::no_diff]] applied to a sub-expression that calls a non-differentiable
// intrinsic (asuint). Without the attribute, the backward-mode rewriter would
// mark the whole function non-differentiable and emit a `_Static_assert`
// stub. The attribute lets the user explicitly tell the rewriter to keep
// the offending sub-expression as-is and proceed with the rest of the
// translation.

// CHECK: namespace user { namespace ad { namespace bwd {
// CHECK: Variable<float> f(inout GradientContext<float> context, Variable<float> x)
// CHECK: VariableExpr<float> x_expr = makeVariableExpr<float>(x);
// CHECK: return add<float>(x_expr, (float)asuint(x));
// CHECK-NOT: _Static_assert
// CHECK: } } } // namespace user::ad::bwd

[[dxc::autodiff(bwd)]]
float f(float x) {
  return x + [[dxc::no_diff]] (float)asuint(x);
}

float main(float x : A) : SV_Target { return f(x); }

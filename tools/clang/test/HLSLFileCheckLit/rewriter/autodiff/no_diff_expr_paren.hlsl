// RUN: %dxr -generate-differentials %s | FileCheck %s
//
// [[dxc::no_diff]] applied to a parenthesised sub-expression. The attribute
// must propagate through ParenExpr / ImplicitCastExpr wrappers so the
// rewriter still recognises the marked node as no-diff. Here the entire
// `(sin(x) * cos(x))` is preserved verbatim instead of being rewritten to
// the backward-mode builder chain.

// CHECK: namespace user { namespace ad { namespace bwd {
// CHECK: Variable<float> f(inout GradientContext<float> context, Variable<float> x)
// CHECK: return add<float>(x_expr, sin(x) * cos(x));
// CHECK-NOT: multiply<float>(sinExpr
// CHECK: } } } // namespace user::ad::bwd

[[dxc::autodiff(bwd)]]
float f(float x) {
  return x + [[dxc::no_diff]] (sin(x) * cos(x));
}

float main(float x : A) : SV_Target { return f(x); }

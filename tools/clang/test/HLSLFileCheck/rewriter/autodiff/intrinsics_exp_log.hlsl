// RUN: %dxr -generate-differentials %s | FileCheck %s
//
// RUN: %dxr -generate-differentials %s > %t.gen.hlsl
// RUN: cat %S/Inputs/autodiff_verify_stubs.hlsli %t.gen.hlsl > %t.full.hlsl
// RUN: echo '// expected-no-diagnostics' >> %t.full.hlsl
// RUN: %dxc -T ps_6_0 -verify %t.full.hlsl

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

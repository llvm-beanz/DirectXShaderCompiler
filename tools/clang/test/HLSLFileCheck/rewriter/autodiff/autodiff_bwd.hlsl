// RUN: %dxr -generate-differentials %s | FileCheck %s
//
// RUN: %dxr -generate-differentials %s > %t.gen.hlsl
// RUN: cat %S/Inputs/autodiff_verify_stubs.hlsli %t.gen.hlsl > %t.full.hlsl
// RUN: echo '// expected-no-diagnostics' >> %t.full.hlsl
// RUN: %dxc -T ps_6_0 -verify %t.full.hlsl

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

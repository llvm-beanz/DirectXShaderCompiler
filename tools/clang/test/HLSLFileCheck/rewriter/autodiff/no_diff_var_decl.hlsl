// RUN: %dxr -generate-differentials %s | FileCheck %s

// [[dxc::no_diff]] applied to a block containing a variable declaration.
// HLSL parses leading attributes on declarations through ParseDeclaration,
// which drops statement attributes that aren't decl attributes, so the
// attribute is applied to the surrounding block instead. The declaration
// (and the rest of the block body) is copied verbatim into both generated
// functions.

// CHECK: namespace user { namespace ad { namespace fwd {
// CHECK: Value<float> f(Value<float> x)
// CHECK: {
// CHECK: float a = x * x;
// CHECK: float b = a + x;
// CHECK: return b;
// CHECK: }
// CHECK: } } } // namespace user::ad::fwd
// CHECK: namespace user { namespace ad { namespace bwd {
// CHECK: Variable<float> f(inout GradientContext<float> context, Variable<float> x)
// CHECK: VariableExpr<float> x_expr = makeVariableExpr<float>(x);
// CHECK: {
// CHECK: float a = x * x;
// CHECK: float b = a + x;
// CHECK: return b;
// CHECK: }
// CHECK: } } } // namespace user::ad::bwd

[[dxc::autodiff(fwd, bwd)]]
float f(float x) {
  [[dxc::no_diff]] {
    float a = x * x;
    float b = a + x;
    return b;
  }
}

float main(float x : A) : SV_Target { return f(x); }

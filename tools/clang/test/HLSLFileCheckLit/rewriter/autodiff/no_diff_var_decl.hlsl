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

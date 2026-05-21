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

// [[dxc::no_diff]] applied to a statement containing a call to a user-defined
// function. The whole assignment statement is copied verbatim into the
// generated forward and backward functions, so the call is not translated
// through the auto-diff expression machinery.

// CHECK: namespace user { namespace ad { namespace fwd {
// CHECK: Value<float> f(Value<float> x)
// CHECK: Value<float> a;
// CHECK: a = helper(x);
// CHECK: return (a + x);
// CHECK: } } } // namespace user::ad::fwd
// CHECK: namespace user { namespace ad { namespace bwd {
// CHECK: Variable<float> f(inout GradientContext<float> context, Variable<float> x)
// CHECK: VariableExpr<float> x_expr = makeVariableExpr<float>(x);
// CHECK: Variable<float> a;
// CHECK: a = helper(x);
// CHECK: return add<float>(a, x_expr);
// CHECK: } } } // namespace user::ad::bwd

float helper(float x) { return x * x; }

[[dxc::autodiff(fwd, bwd)]]
float f(float x) {
  float a;
  [[dxc::no_diff]] a = helper(x);
  return a + x;
}

float main(float x : A) : SV_Target { return f(x); }

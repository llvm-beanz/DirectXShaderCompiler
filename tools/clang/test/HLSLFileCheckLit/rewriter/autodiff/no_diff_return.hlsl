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

// The [[dxc::no_diff]] statement attribute marks a statement that should NOT be
// translated by the auto-diff rewriter; the wrapped statement is copied
// verbatim into the generated function. The attribute can be applied to any
// statement that wraps into an AttributedStmt -- typically a compound block,
// an expression statement, or a return statement.

// CHECK: namespace user { namespace ad { namespace fwd {
// CHECK: Value<float> f(Value<float> x)
// CHECK: return x * x + x;
// CHECK: } } } // namespace user::ad::fwd
// CHECK: namespace user { namespace ad { namespace bwd {
// CHECK: Variable<float> f(inout GradientContext<float> context, Variable<float> x)
// CHECK: VariableExpr<float> x_expr = makeVariableExpr<float>(x);
// CHECK: return x * x + x;
// CHECK: } } } // namespace user::ad::bwd

[[dxc::autodiff(fwd, bwd)]]
float f(float x) {
  [[dxc::no_diff]] return x * x + x;
}

float main(float x : A) : SV_Target { return f(x); }

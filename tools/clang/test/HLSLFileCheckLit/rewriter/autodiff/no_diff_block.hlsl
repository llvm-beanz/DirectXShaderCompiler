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

// [[dxc::no_diff]] applied to a compound statement: the rewriter copies the entire
// block verbatim into both forward and backward generated functions.

// CHECK: namespace user { namespace ad { namespace fwd {
// CHECK: Value<float> f(Value<float> x)
// CHECK: {
// CHECK: float t = x * x;
// CHECK: return t + x;
// CHECK: }
// CHECK: } } } // namespace user::ad::fwd

[[dxc::autodiff(fwd)]]
float f(float x) {
  [[dxc::no_diff]] {
    float t = x * x;
    return t + x;
  }
}

float main(float x : A) : SV_Target { return f(x); }

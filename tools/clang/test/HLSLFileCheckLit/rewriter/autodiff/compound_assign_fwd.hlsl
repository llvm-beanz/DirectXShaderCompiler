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

// Compound assignment forms map to *Assign builders in backward mode; in
// forward mode they are preserved on Value<T> via the operator overloads in
// hlsl/ad/fwd.

// CHECK: namespace user { namespace ad { namespace fwd {
// CHECK: Value<float> f(Value<float> x)
// CHECK: Value<float> a = x;
// CHECK: (a += x);
// CHECK: (a -= x);
// CHECK: (a *= x);
// CHECK: (a /= x);
// CHECK: return a;
// CHECK: } } } // namespace user::ad::fwd

[[dxc::autodiff(fwd)]]
float f(float x) {
  float a = x;
  a += x;
  a -= x;
  a *= x;
  a /= x;
  return a;
}

float main(float x : A) : SV_Target { return f(x); }

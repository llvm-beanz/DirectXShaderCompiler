// RUN: %dxr -generate-differentials %s | FileCheck %s

// [[no_diff]] applied to a compound statement: the rewriter copies the entire
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
  [[no_diff]] {
    float t = x * x;
    return t + x;
  }
}

float main(float x : A) : SV_Target { return f(x); }

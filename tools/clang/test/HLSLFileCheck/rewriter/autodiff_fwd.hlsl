// RUN: %dxr -generate-differentials %s | FileCheck %s

// Forward-only autodiff: the original function is preserved and a
// Value<float>-based forward variant is emitted in user::ad::fwd.

// CHECK: float f(float x)
// CHECK: namespace user { namespace ad { namespace fwd {
// CHECK: Value<float> f(Value<float> x)
// CHECK: return ((x * x) + x);
// CHECK: } } } // namespace user::ad::fwd

[[dxc::autodiff(fwd)]]
float f(float x) {
  return x * x + x;
}

float main(float x : A) : SV_Target { return f(x); }

// RUN: %dxr -generate-differentials %s | FileCheck %s
// RUN: %dxr -generate-differentials %s > %t.gen.hlsl
// RUN: %dxc -T ps_6_9 -HV 2021 -Fo %t.dxil %t.gen.hlsl

// [[dxc::no_diff]] applied to a compound statement lowers every expression
// in the block through primal values.

// CHECK: namespace user { namespace ad { namespace fwd {
// CHECK: Value<float> f(Value<float> x)
// CHECK: Value<float> t = Value<float>::CreateValue((x.value * x.value));
// CHECK: return Value<float>::CreateValue((t.value + x.value));
// CHECK: } } } // namespace user::ad::fwd

[[dxc::autodiff(fwd)]]
float f(float x) {
  [[dxc::no_diff]] {
    float t = x * x;
    return t + x;
  }
}

float main(float x : A) : SV_Target { return f(x); }

// RUN: %dxr -generate-differentials %s | FileCheck %s
//
// RUN: %dxr -generate-differentials %s > %t.gen.hlsl
// RUN: cat %S/Inputs/autodiff_verify_stubs.hlsli %t.gen.hlsl > %t.full.hlsl
// RUN: echo '// expected-no-diagnostics' >> %t.full.hlsl
// RUN: %dxc -T ps_6_0 -verify %t.full.hlsl

// Exponential / logarithm / power family in forward mode: the calls remain
// untransformed, since the Value<T> overloads supply the differential
// arithmetic.

// CHECK: namespace user { namespace ad { namespace fwd {
// CHECK: Value<float> f(Value<float> x, Value<float> y)
// CHECK: exp(x)
// CHECK: exp2(x)
// CHECK: log(x)
// CHECK: log2(x)
// CHECK: log10(x)
// CHECK: pow(x, y)
// CHECK: } } } // namespace user::ad::fwd

[[dxc::autodiff(fwd)]]
float f(float x, float y) {
  return exp(x) + exp2(x)
       + log(x) + log2(x) + log10(x)
       + pow(x, y);
}

float main(float x : A) : SV_Target { return f(x, x); }

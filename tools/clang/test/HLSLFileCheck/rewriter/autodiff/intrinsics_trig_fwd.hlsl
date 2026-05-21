// RUN: %dxr -generate-differentials %s | FileCheck %s
//
// RUN: %dxr -generate-differentials %s > %t.gen.hlsl
// RUN: cat %S/Inputs/autodiff_verify_stubs.hlsli %t.gen.hlsl > %t.full.hlsl
// RUN: echo '// expected-no-diagnostics' >> %t.full.hlsl
// RUN: %dxc -T ps_6_0 -verify %t.full.hlsl

// Trig intrinsics in forward mode are preserved as-is on Value<T> -- the
// hlsl/ad/fwd library overloads sin/cos/... directly. No '*Expr' rename
// happens (that is a backward-mode concern only).

// CHECK: namespace user { namespace ad { namespace fwd {
// CHECK: Value<float> f(Value<float> x)
// CHECK: sin(x)
// CHECK: cos(x)
// CHECK: tan(x)
// CHECK: asin(x)
// CHECK: acos(x)
// CHECK: atan(x)
// CHECK: sinh(x)
// CHECK: cosh(x)
// CHECK: tanh(x)
// CHECK: } } } // namespace user::ad::fwd

[[dxc::autodiff(fwd)]]
float f(float x) {
  return sin(x) + cos(x) + tan(x)
       + asin(x) + acos(x) + atan(x)
       + sinh(x) + cosh(x) + tanh(x);
}

float main(float x : A) : SV_Target { return f(x); }

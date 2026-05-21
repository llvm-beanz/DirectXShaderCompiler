// RUN: %dxr -generate-differentials %s | FileCheck %s

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

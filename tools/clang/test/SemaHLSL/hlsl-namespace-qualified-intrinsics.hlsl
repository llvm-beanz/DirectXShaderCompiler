// RUN: %dxc -T lib_6_6 -HV 202x %s -verify

// Under HLSL 202x, qualifying an intrinsic name with the 'hlsl::' namespace
// must compile cleanly once an unqualified call has materialized the
// declaration inside the namespace.  This file is a -verify run so any
// unexpected diagnostic will cause it to fail.

// expected-no-diagnostics

float UseSin(float x) {
  // First an unqualified use to materialize the declaration in 'hlsl'.
  float u = sin(x);
  // Then a fully qualified call must also succeed.
  float q = hlsl::sin(x);
  return u + q;
}

float UseDot(float3 a, float3 b) {
  float u = dot(a, b);
  float q = hlsl::dot(a, b);
  return u + q;
}

[shader("compute")]
[numthreads(1,1,1)]
void main() {
  UseSin(0.5);
  UseDot(float3(1, 0, 0), float3(0, 1, 0));
}

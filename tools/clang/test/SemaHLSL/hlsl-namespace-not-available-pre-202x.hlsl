// RUN: %dxc -T lib_6_6 -HV 2021 %s -verify

// In HLSL 2021 the implicit 'hlsl' namespace does not exist.  Attempting to
// reference an intrinsic through the 'hlsl::' qualifier must produce a
// diagnostic, while unqualified usage continues to work.

[shader("compute")]
[numthreads(1,1,1)]
void main() {
  float a = sin(0.5);           // OK
  float b = hlsl::sin(0.5);     // expected-error{{use of undeclared identifier 'hlsl'}}
}

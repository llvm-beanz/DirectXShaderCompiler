// RUN: %dxc -T ps_6_0 -E main -HV 2021 -verify %s

// Verify that pre-HLSL 202x rejects `const`-qualified instance methods.

struct S {
  int x;
  int get() const { return x; } // expected-error {{expected ';' at end of declaration list}}
};

float4 main() : SV_Target {
  S s = {1};
  return s.get();
}

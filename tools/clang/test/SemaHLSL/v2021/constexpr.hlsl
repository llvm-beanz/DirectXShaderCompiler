// RUN: %dxc -T lib_6_3 -HV 2021 -verify %s

// 'constexpr' is not supported in HLSL versions prior to 202x.
constexpr int g_constexpr = 3; // expected-error {{'constexpr' is only supported in HLSL 202x or later}}

constexpr int square(int x) { return x * x; } // expected-error {{'constexpr' is only supported in HLSL 202x or later}}

[shader("compute")]
[numthreads(1,1,1)]
void main() {
  constexpr int local = 5; // expected-error {{'constexpr' is only supported in HLSL 202x or later}}
  (void)local;
}

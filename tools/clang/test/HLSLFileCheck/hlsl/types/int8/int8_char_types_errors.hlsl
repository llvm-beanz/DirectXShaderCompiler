// RUN: %dxc -E main -T ps_6_0 -verify %s

// Test that 'char' is rejected as a reserved keyword below SM 6.10.

char bad_char; // expected-error{{'char' is a reserved keyword in HLSL}} expected-error{{HLSL requires a type specifier for all declarations}}

float4 main() : SV_Target { return 0; }

// RUN: %dxc -E main -T ps_6_0 -verify %s

// Test that int8_t and uint8_t are rejected below SM 6.10.

int8_t  bad_int8;  // expected-error{{int8_t is only allowed for HLSL shader model 6.10 and above.}}
uint8_t bad_uint8; // expected-error{{uint8_t is only allowed for HLSL shader model 6.10 and above.}}

float4 main() : SV_Target { return 0; }

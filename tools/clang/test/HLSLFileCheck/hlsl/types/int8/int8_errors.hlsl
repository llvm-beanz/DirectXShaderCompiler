// RUN: not %dxc -E main -T ps_6_0 %s 2>&1 | FileCheck %s

// Test that int8_t and uint8_t are rejected below SM 6.10.
// CHECK: int8_t is only allowed for HLSL shader model 6.10 and above.
// CHECK: uint8_t is only allowed for HLSL shader model 6.10 and above.

int8_t  bad_int8;
uint8_t bad_uint8;

float4 main() : SV_Target { return 0; }

// RUN: %dxc -T ps_6_10 -E main %s | FileCheck %s

// Test cbuffer packing for int8_t and uint8_t types.
// 4 int8_t values should pack into a single 32-bit slot.
// Reads use cbufferLoadLegacy.i32 with shift/mask to extract each byte.

// CHECK: ; int8_t a;{{.*}}; Offset:    0
// CHECK: ; int8_t b;{{.*}}; Offset:    1
// CHECK: ; int8_t c;{{.*}}; Offset:    2
// CHECK: ; int8_t d;{{.*}}; Offset:    3
// CHECK: ; int e;{{.*}}; Offset:    4
// CHECK: ; int8_t g;{{.*}}; Offset:    8
// CHECK: ; uint8_t h;{{.*}}; Offset:    9

struct S8 {
  int8_t a;   // byte 0
  int8_t b;   // byte 1
  int8_t c;   // byte 2
  int8_t d;   // byte 3
  int   e;    // bytes 4-7
};

cbuffer CB : register(b0) {
  S8 s;
  int8_t  g;  // byte 8
  uint8_t h;  // byte 9
};

// All cbuffer data fits in one 16-byte register (10 bytes total).
// CHECK: define void @main
// CHECK: call %dx.types.CBufRet.i32 @dx.op.cbufferLoadLegacy.i32(i32 59, {{.*}}, i32 0)
// CHECK-NOT: @dx.op.cbufferLoadLegacy.i8
// CHECK-NOT: @dx.op.cbufferLoadLegacy.i32(i32 59, {{.*}}, i32 1)

// s.a/b/c/d are in slot 0; s.e is in slot 1; g/h are in slot 2.
// CHECK: extractvalue %dx.types.CBufRet.i32 {{.*}}, 0
// CHECK: extractvalue %dx.types.CBufRet.i32 {{.*}}, 1
// CHECK: extractvalue %dx.types.CBufRet.i32 {{.*}}, 2

// g (int8_t, byte 8 = slot 2 byte 0): sign-extend via shl 24 / ashr 24.
// CHECK: shl i32 {{.*}}, 24
// CHECK: ashr {{.*}} i32 {{.*}}, 24

// h (uint8_t, byte 9 = slot 2 byte 1): zero-extend: lshr 8, and 255.
// CHECK: lshr i32 {{.*}}, 8
// CHECK: and i32 {{.*}}, 255

float4 main() : SV_Target {
  int r = s.a + s.b + s.c + s.d + s.e + g + h;
  return r;
}

// COPILOT-TODO: We should test the positioning of vectors of int8/uint8 which
// should require 32-bit alignment and not pack across 4-byte boundaries. We
// should also test that arrays of int8/uint8 follow cbuffer array packing rules
// (each element aligned to 16 bytes, no packing across elements).

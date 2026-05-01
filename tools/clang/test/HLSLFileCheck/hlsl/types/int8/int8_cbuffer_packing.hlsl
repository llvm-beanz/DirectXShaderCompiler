// RUN: %dxc -T ps_6_10 -E main %s | FileCheck %s

// Test cbuffer packing rules for int8_t/uint8_t vectors and arrays.
//
// Vectors: each vector occupies one full 16-byte register slot.
//   int8_t2 v2  => Offset:  0  (slot 0)
//   int8_t4 v4  => Offset: 16  (slot 1)
//   int     pad => Offset: 32  (slot 2, component 0)
//
// CHECK: ; int8_t2 v2;{{.*}}; Offset:    0
// CHECK: ; int8_t4 v4;{{.*}}; Offset:   16
// CHECK: ; int pad;{{.*}}; Offset:   32
//
// Arrays: each element of an int8_t array occupies its own 16-byte slot.
//   int8_t arr[4]: arr[0] at Offset 0, arr[1] at 16, arr[2] at 32, arr[3] at 48.
//   int after_arr: packs into the remaining space of the last slot => Offset: 52.
//
// CHECK: ; int8_t arr[4];{{.*}}; Offset:    0
// CHECK: ; int after_arr;{{.*}}; Offset:   52

cbuffer CBVec : register(b0) {
  int8_t2 v2;
  int8_t4 v4;
  int     pad;
};

cbuffer CBArr : register(b1) {
  int8_t arr[4];
  int    after_arr;
};

// CHECK-LABEL: define void @main

// v2 is in CBVec slot 0 (regIndex=0); v4 is in slot 1 (regIndex=1);
// pad is in slot 2 (regIndex=2).
// CHECK: cbufferLoadLegacy.i32{{.*}}, i32 0)
// CHECK: cbufferLoadLegacy.i32{{.*}}, i32 1)
// CHECK: cbufferLoadLegacy.i32{{.*}}, i32 2)

// v2.x (int8_t at byte 0 of slot 0): sign-extend via shl 24 / ashr 24.
// CHECK: shl i32 {{.*}}, 24
// CHECK: ashr {{.*}} i32 {{.*}}, 24

// arr[0] is in CBArr slot 0 (regIndex=0); arr[3] and after_arr are in slot 3.
// CHECK: cbufferLoadLegacy.i32{{.*}}, i32 3)

// arr[3] (int8_t at byte 0 of slot 3): sign-extend via shl 24 / ashr 24.
// CHECK: shl i32 {{.*}}, 24
// CHECK: ashr {{.*}} i32 {{.*}}, 24
// after_arr is component 1 of slot 3.
// CHECK: extractvalue %dx.types.CBufRet.i32 {{.*}}, 1

float4 main() : SV_Target {
  int r = v2.x + v4.x + pad + arr[0] + arr[3] + after_arr;
  return r;
}

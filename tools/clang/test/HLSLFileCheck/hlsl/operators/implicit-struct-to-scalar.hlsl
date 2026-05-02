// RUN: %dxc -T cs_6_6 -HV 2021 -enable-16bit-types -fcgl %s | FileCheck %s

// Test that casting a struct to a scalar type (FlatConversion) works correctly.
// The struct is implicitly flattened using just its first member.

struct Color {
  uint16_t r;
  uint16_t g;
  uint16_t b;
};

RWStructuredBuffer<uint> buf : r0;

[numthreads(4, 8, 16)]
void main() {
  Color s;
  s.r = 4;
  s.g = 5;
  s.b = 6;
  uint64_t value = (uint)s;
}

// CHECK: define void @main()
// CHECK: %s = alloca %struct.Color
// CHECK: %value = alloca i64

// Store the fields
// CHECK: store i16 4
// CHECK: store i16 5
// CHECK: store i16 6

// Load first field for the FlatConversion cast: only 'r' is used
// CHECK: %[[R:[0-9]+]] = load i16
// CHECK: %[[ZR:[0-9]+]] = zext i16 %[[R]] to i32
// CHECK: store i32 %[[ZR]]
// Extend to uint64_t
// CHECK: %[[UINT:[0-9]+]] = load i32
// CHECK: %[[U64:[0-9]+]] = zext i32 %[[UINT]] to i64
// CHECK: store i64 %[[U64]], i64* %value

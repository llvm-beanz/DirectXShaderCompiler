// RUN: %dxc -E main -T cs_6_10 -verify %s

// Test that implicit narrowing conversions to int8_t/uint8_t produce warnings,
// while widening conversions from int8_t/uint8_t to larger types do not.

RWStructuredBuffer<int8_t>  i8buf   : register(u0);
RWStructuredBuffer<uint8_t> u8buf   : register(u1);
RWStructuredBuffer<int>     i32buf  : register(u2);
RWStructuredBuffer<uint>    u32buf  : register(u3);
RWStructuredBuffer<int8_t4> i8v4buf : register(u4);
RWStructuredBuffer<int4>    i32v4buf : register(u5);

[numthreads(1, 1, 1)]
void main() {
  // Implicit int32 -> int8: narrowing, expect warning.
  int big = i32buf[0];
  i8buf[0] = big; // expected-warning{{conversion from larger type 'int' to smaller type 'signed char', possible loss of data}}

  // Implicit uint32 -> uint8: narrowing, expect warning.
  uint ubig = u32buf[0];
  u8buf[0] = ubig; // expected-warning{{conversion from larger type 'uint' to smaller type 'unsigned char', possible loss of data}}

  // Implicit int8 -> int32: widening, no warning expected.
  int8_t small = i8buf[1];
  i32buf[1] = small;

  // Implicit uint8 -> uint32: widening, no warning expected.
  uint8_t usmall = u8buf[1];
  u32buf[1] = usmall;

  // Implicit int4 -> int8_t4 vector: narrowing, expect warning.
  int4 bigvec = i32v4buf[0];
  i8v4buf[0] = bigvec; // expected-warning{{conversion from larger type 'int4' to smaller type 'vector<signed char, 4>', possible loss of data}}
}

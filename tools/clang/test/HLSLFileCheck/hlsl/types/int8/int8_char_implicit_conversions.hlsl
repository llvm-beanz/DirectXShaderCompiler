// RUN: %dxc -E main -T cs_6_10 -verify %s

// Test that implicit narrowing conversions to 'char'/'unsigned char' produce
// warnings just like int8_t/uint8_t, while widening conversions do not.

RWStructuredBuffer<char>          cbuf   : register(u0);
RWStructuredBuffer<unsigned char> ucbuf  : register(u1);
RWStructuredBuffer<int>           i32buf : register(u2);
RWStructuredBuffer<uint>          u32buf : register(u3);

[numthreads(1, 1, 1)]
void main() {
  // Narrowing int -> char: expect warning.
  int big = i32buf[0];
  cbuf[0] = big; // expected-warning{{conversion from larger type 'int' to smaller type 'char', possible loss of data}}

  // Narrowing uint -> unsigned char: expect warning.
  uint ubig = u32buf[0];
  ucbuf[0] = ubig; // expected-warning{{conversion from larger type 'uint' to smaller type 'unsigned char', possible loss of data}}

  // Widening char -> int: no warning expected.
  char c = cbuf[1];
  i32buf[1] = c;

  // Widening unsigned char -> uint: no warning expected.
  unsigned char uc = ucbuf[1];
  u32buf[1] = uc;
}

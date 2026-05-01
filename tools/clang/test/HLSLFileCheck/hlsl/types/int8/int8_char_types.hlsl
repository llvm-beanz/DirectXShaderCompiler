// RUN: %dxc -E main -T cs_6_10 %s | FileCheck %s

// Test that 'char' and 'unsigned char' are valid alternatives to int8_t and
// uint8_t in SM 6.10 and map to the same i8 DXIL types.

RWStructuredBuffer<char>          cbuf   : register(u0);
RWStructuredBuffer<unsigned char> ucbuf  : register(u1);
RWStructuredBuffer<int>           i32buf : register(u2);
RWStructuredBuffer<uint>          u32buf : register(u3);

[numthreads(1, 1, 1)]
void main() {
  // Load char: uses rawBufferLoad.i8, same as int8_t.
  // CHECK: rawBufferLoad.i8
  char c = cbuf[0];

  // Widen char -> int: sign-extends (sext), same as int8_t -> int.
  // CHECK: sext i8
  i32buf[0] = c;

  // Load unsigned char: uses rawBufferLoad.i8, same as uint8_t.
  // CHECK: rawBufferLoad.i8
  unsigned char uc = ucbuf[0];

  // Widen unsigned char -> uint: zero-extends (zext), same as uint8_t -> uint.
  // CHECK: zext i8
  u32buf[0] = uc;

  // Store char: uses rawBufferStore.i8, same as int8_t.
  // CHECK: rawBufferStore.i8
  cbuf[1] = c;

  // Store unsigned char: uses rawBufferStore.i8, same as uint8_t.
  // CHECK: rawBufferStore.i8
  ucbuf[1] = uc;
}

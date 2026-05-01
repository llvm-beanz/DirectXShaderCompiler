// RUN: not %dxc -E main -T cs_6_10 %s 2>&1 | FileCheck %s

// Test that int8_t is rejected in typed buffers (Buffer<>, Texture2D<>, etc.)
// and allowed in raw/structured buffers.

// CHECK: elements of typed buffers and textures must be scalars or vectors
Buffer<int8_t> typed_buf : register(t0);

RWStructuredBuffer<int8_t> structured_buf : register(u0);  // OK

[numthreads(1, 1, 1)]
void main() {
  structured_buf[0] = 42;
}

// COPILOT-TODO: Diagnostic tests should use the -verify option and check for
// specific error messages with // expected-error comments, rather than relying
// on FileCheck to match error text in the compiler output.

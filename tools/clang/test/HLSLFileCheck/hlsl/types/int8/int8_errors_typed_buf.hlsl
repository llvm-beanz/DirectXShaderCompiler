// RUN: %dxc -E main -T cs_6_10 -verify %s

// Test that int8_t is rejected in typed buffers (Buffer<>, Texture2D<>, etc.)
// and allowed in raw/structured buffers.

Buffer<int8_t> typed_buf : register(t0); // expected-error{{elements of typed buffers and textures must be scalars or vectors}} expected-error{{invalid register specification, expected 'b', 'c', or 'i' binding}}

RWStructuredBuffer<int8_t> structured_buf : register(u0);  // OK

[numthreads(1, 1, 1)]
void main() {
  structured_buf[0] = 42;
}

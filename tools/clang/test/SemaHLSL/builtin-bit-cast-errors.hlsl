// RUN: %dxc -Tlib_6_8 -verify %s

RWBuffer<float> g_buf;
struct S { float a; float b; };

[shader("compute")]
[numthreads(1,1,1)]
void main(uint3 tid : SV_DispatchThreadID) {
  float f = 1.5f;

  // Same-size scalar bit cast OK.
  uint u = __builtin_bit_cast(uint, f);

  // Size mismatch.
  uint2 u2 = __builtin_bit_cast(uint2, f); // expected-error{{__builtin_bit_cast source type 'float' (32 bits) and destination type 'uint2' (64 bits) must have the same size}}

  // Intangible destination.
  RWBuffer<float> r = __builtin_bit_cast(RWBuffer<float>, g_buf); // expected-error{{__builtin_bit_cast cannot be used with intangible type 'RWBuffer<float>'}}

  // Intangible source.
  uint64_t h = __builtin_bit_cast(uint64_t, g_buf); // expected-error{{__builtin_bit_cast cannot be used with intangible type 'RWBuffer<float>'}}

  // Aggregate currently unsupported.
  S s; s.a = 0; s.b = 0;
  uint2 us = __builtin_bit_cast(uint2, s); // expected-error{{__builtin_bit_cast currently supports only scalar and vector types; got 'S'}}
}

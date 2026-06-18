// RUN: %dxc -Tlib_6_8 -verify %s

#include <bit_cast.h>

RWBuffer<float> g_buf;

[shader("compute")]
[numthreads(1,1,1)]
void main() {
  float f = 1.0f;
  uint u = hlsl::bit_cast<uint>(f); // OK

  // Size mismatch instantiates the template but the builtin diagnoses.
  // expected-error@bit_cast.h:* {{__builtin_bit_cast source type 'float' (32 bits) and destination type 'vector<unsigned int, 2>' (64 bits) must have the same size}}
  // expected-note@+1 {{in instantiation}}
  uint2 u2 = hlsl::bit_cast<uint2>(f);

  // Intangible source: diagnosed at the template body.
  // expected-error@bit_cast.h:* {{__builtin_bit_cast cannot be used with intangible type 'RWBuffer<float>'}}
  // expected-note@+1 {{in instantiation}}
  uint64_t h = hlsl::bit_cast<uint64_t>(g_buf);
}

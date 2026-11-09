// RUN: %dxc -T cs_6_0 -E main -HV 202x -verify %s

RWByteAddressBuffer Output : register(u0);

[numthreads(1, 1, 1)]
void main() {
  Output.Store(0, dx::__builtin_hlsl_string_to_offset("hello"));
  // expected-error@+1 {{use of undeclared identifier '__builtin_hlsl_string_to_offset'}}
  Output.Store(4, __builtin_hlsl_string_to_offset("hello"));
}

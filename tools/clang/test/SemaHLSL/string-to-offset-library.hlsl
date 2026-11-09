// RUN: %dxc -T lib_6_3 -HV 202x -verify %s
// RUN: %dxc -T lib_6_8 -HV 202x -verify %s

export uint StringOffset() {
  // expected-error@+1 {{'__builtin_hlsl_string_to_offset' is unavailable: not supported in library shaders}}
  return dx::__builtin_hlsl_string_to_offset("hello");
}

static uint UnusedOffset() {
  return dx::__builtin_hlsl_string_to_offset("unused");
}

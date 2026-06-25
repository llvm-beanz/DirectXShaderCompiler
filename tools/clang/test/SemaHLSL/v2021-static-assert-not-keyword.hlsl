// RUN: %dxc -T lib_6_3 -HV 2021 -verify %s

// In HLSL versions earlier than 202x, 'static_assert' is not a keyword
// and is parsed as an ordinary identifier. The C11-style
// '_Static_assert' continues to work in all HLSL versions and is
// covered elsewhere; this test only checks that the C++11 spelling is
// not silently enabled before 202x.

static_assert(1 == 1, "should not be recognized as a keyword"); // expected-error{{HLSL requires a type specifier for all declarations}} expected-error{{expected parameter declarator}} expected-error{{expected ')'}} expected-note{{to match this '('}}

// RUN: %dxc -Tlib_6_8 -verify %s

// Negative tests for HLSL type traits.

struct Incomplete; // expected-note{{forward declaration of 'Incomplete'}}
_Static_assert(__is_intangible(Incomplete), ""); // expected-error{{incomplete type 'Incomplete' used in type trait expression}}

// Unsupported standard type traits remain unsupported in HLSL.
bool x = __is_pod(int); // expected-error{{__is_pod is unsupported in HLSL}}

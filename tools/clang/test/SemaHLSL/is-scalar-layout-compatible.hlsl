// RUN: %dxc -Tlib_6_8 -verify %s

// Tests for the __is_scalar_layout_compatible HLSL type trait.

struct Two { float a; int b; };
struct TwoCompat { int a; float b; };
struct Three { float a; int b; int c; };
struct Nested { Two t; float c; };

// expected-no-diagnostics

_Static_assert(__is_scalar_layout_compatible(float, int), "scalar arithmetic compatible");
_Static_assert(__is_scalar_layout_compatible(float, float), "same scalar type");
_Static_assert(__is_scalar_layout_compatible(int2, float2), "vector pair compatible");
_Static_assert(__is_scalar_layout_compatible(Two, TwoCompat), "matching record layouts");
_Static_assert(__is_scalar_layout_compatible(float[3], Three), "record vs same-shaped array");
_Static_assert(__is_scalar_layout_compatible(Nested, float3), "nested record flattens to vector");
_Static_assert(__is_scalar_layout_compatible(float2x2, float[4]), "matrix to array");

_Static_assert(!__is_scalar_layout_compatible(float, int2), "scalar vs vector mismatch");
_Static_assert(!__is_scalar_layout_compatible(Two, Three), "different element counts");
_Static_assert(!__is_scalar_layout_compatible(RWBuffer<float>, RWBuffer<float>), "intangible never compatible");

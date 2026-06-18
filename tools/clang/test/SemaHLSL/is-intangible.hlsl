// RUN: %dxc -Tlib_6_8 -verify %s

// Tests for the __is_intangible HLSL type trait.

struct Numeric { float f; int i; };
struct WithResource { float x; RWBuffer<float> r; };
struct WithRayQuery { RayQuery<0> rq; };
struct DerivedFromRes : WithResource { int j; };

// expected-no-diagnostics

_Static_assert(__is_intangible(RWBuffer<float>), "RWBuffer is intangible");
_Static_assert(__is_intangible(Texture2D), "Texture2D is intangible");
_Static_assert(__is_intangible(SamplerState), "SamplerState is intangible");
_Static_assert(__is_intangible(WithResource), "record containing a resource is intangible");
_Static_assert(__is_intangible(DerivedFromRes), "derived from intangible record is intangible");
_Static_assert(__is_intangible(WithRayQuery), "record containing RayQuery is intangible");
_Static_assert(__is_intangible(RWBuffer<float>[4]), "array of intangible is intangible");

_Static_assert(!__is_intangible(int), "int is not intangible");
_Static_assert(!__is_intangible(float), "float is not intangible");
_Static_assert(!__is_intangible(half3), "half3 is not intangible");
_Static_assert(!__is_intangible(float4x4), "float4x4 is not intangible");
_Static_assert(!__is_intangible(Numeric), "purely numeric record is not intangible");
_Static_assert(!__is_intangible(float[16]), "array of floats is not intangible");

// RUN: %dxc -E main -T ps_6_0 -HV 202x %s | FileCheck %s

// HLSL 202x's variadic templates do not extend to pack expansion in a
// base-specifier list: HLSL only ever supports a single, fixed base type
// per struct, so `struct D : Bases... {}` remains rejected even though
// HLSL 202x otherwise treats `...` like C++11 variadic templates.

// CHECK: error: base type ellipsis is unsupported in HLSL
// CHECK: error: multiple concrete base types specified

struct Base1 {
  float x;
};
struct Base2 {
  float y;
};

template <typename... Bases>
struct Derived : Bases... {
  float z;
};

float main() : SV_Target {
  Derived<Base1, Base2> d;
  d.z = 1.0;
  return d.z;
}

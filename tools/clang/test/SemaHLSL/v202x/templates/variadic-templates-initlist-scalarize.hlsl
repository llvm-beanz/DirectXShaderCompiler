// RUN: %dxc -T lib_6_3 -HV 202x -verify %s
// RUN: %dxc -T lib_6_3 -HV 202x -ast-dump %s 2>&1 | FileCheck %s

// HLSL's braced-initializer-list handling for scalar/vector/matrix/struct
// targets is unusually permissive: it "scalarizes" (flattens) every
// initializer element -- including aggregate elements like vectors --
// into a flat stream of scalars used to fill the target's scalar slots,
// even allowing a single initializer list to overflow across the
// boundaries of an array of aggregates. This is quite different from
// C/C++ aggregate-initialization rules.
//
// Since a pack expansion inside a braced-init-list
// (`{ First, Others... }`) is expanded into the same flat list of
// individual element expressions that a hand-written initializer list
// would contain, this test verifies that HLSL 202x's scalarizing
// initializer-list semantics behave identically regardless of whether
// the elements originated from an expanded parameter pack or were written
// out by hand -- same successful flattening, same overflow-across-array-
// elements behavior, and the same diagnostics (with the same wording) for
// too-few/too-many element counts.

// expected-no-diagnostics

struct ScalarizeStruct {
  float a;
  float2 b;
  float c;
};

// --- Exact-count vector scalarization: pack vs. hand-written. ---

// CHECK: FunctionDecl {{.*}} used ManualVectorExact 'float4 (float, float, float, float)'
// CHECK: InitListExpr {{.*}} 'float4':'vector<float, 4>'
float4 ManualVectorExact(float a, float b, float c, float d) {
  float4 v = { a, b, c, d };
  return v;
}

// CHECK: FunctionDecl {{.*}} used PackVectorExact 'float4 (float, float, float, float)'
// CHECK: InitListExpr {{.*}} 'float4':'vector<float, 4>'
template <typename... Ts>
float4 PackVectorExact(Ts... vals) {
  float4 v = { vals... };
  return v;
}

// --- Mixed scalar/vector elements are flattened identically whether the
// vector element came from a pack or was written by hand. ---

// CHECK: FunctionDecl {{.*}} used ManualVectorMixed 'float3 (float2, float)'
// CHECK: InitListExpr {{.*}} 'float3':'vector<float, 3>'
float3 ManualVectorMixed(float2 a, float b) {
  float3 v = { a, b };
  return v;
}

// CHECK: FunctionDecl {{.*}} used PackVectorMixed 'float3 (vector<float, 2>, float)'
// CHECK: InitListExpr {{.*}} 'float3':'vector<float, 3>'
template <typename... Ts>
float3 PackVectorMixed(Ts... vals) {
  float3 v = { vals... };
  return v;
}

// --- A flat initializer list can overflow across the boundary of an
// array of aggregates; this must behave the same whether the flat list
// of scalars came from a pack expansion or was written by hand. ---

float2 ManualArrayOverflow(float a, float b, float c, float d) {
  float2 arr[2] = { a, b, c, d };
  return arr[0] + arr[1];
}

template <typename... Ts>
float2 PackArrayOverflow(Ts... vals) {
  float2 arr[2] = { vals... };
  return arr[0] + arr[1];
}

// --- Struct-member scalarization (including a vector-typed member being
// filled from the flat initializer stream) behaves the same for packs. ---

ScalarizeStruct ManualStruct(float a, float2 b, float c) {
  ScalarizeStruct s = { a, b, c };
  return s;
}

template <typename... Ts>
ScalarizeStruct PackStruct(Ts... vals) {
  ScalarizeStruct s = { vals... };
  return s;
}

// --- Matrix scalarization behaves the same for packs. ---

float2x2 ManualMatrix(float a, float b, float c, float d) {
  float2x2 m = { a, b, c, d };
  return m;
}

template <typename... Ts>
float2x2 PackMatrix(Ts... vals) {
  float2x2 m = { vals... };
  return m;
}

// --- Call sites forcing instantiation of all of the pack-based
// templates above with concrete argument types/counts. ---
export
float TestInitListScalarization() {
  float4 v1 = ManualVectorExact(1.0, 2.0, 3.0, 4.0);
  float4 v2 = PackVectorExact(1.0, 2.0, 3.0, 4.0);

  float2 b2 = float2(1.0, 2.0);
  float3 v3 = ManualVectorMixed(b2, 3.0);
  float3 v4 = PackVectorMixed(b2, 3.0);

  float o1 = ManualArrayOverflow(1.0, 2.0, 3.0, 4.0).x;
  float o2 = PackArrayOverflow(1.0, 2.0, 3.0, 4.0).x;

  ScalarizeStruct s1 = ManualStruct(1.0, b2, 2.0);
  ScalarizeStruct s2 = PackStruct(1.0, b2, 2.0);

  float2x2 m1 = ManualMatrix(1.0, 2.0, 3.0, 4.0);
  float2x2 m2 = PackMatrix(1.0, 2.0, 3.0, 4.0);

  return v1.x + v2.x + v3.x + v4.x + o1 + o2 + s1.a + s2.a + m1._11 + m2._11;
}

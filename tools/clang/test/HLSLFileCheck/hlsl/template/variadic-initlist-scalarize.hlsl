// RUN: %dxc -E main -T ps_6_0 -HV 202x %s | FileCheck %s

// End-to-end CodeGen coverage proving that HLSL's initializer-list
// scalarization (flattening aggregate/vector elements into a target's
// scalar slots, including overflow across array-of-aggregate element
// boundaries) reaches DXIL identically whether the flat stream of
// initializer elements came from expanding a variadic template pack or
// was written out by hand.
//
// Each pair of functions below (Pack*/Manual*) is semantically identical
// and is fed the exact same shader-input-dependent values (so nothing is
// constant-folded away at compile time -- a constant-folded result could
// hide a broken CodeGen path behind a "correct by luck" compile-time
// value), then the two results are added together. If pack-expansion
// scalarization produced different results than hand-written
// scalarization, the additions below would not fold down to simple,
// exact-integer multiples of the shader inputs.

struct PairOf2 {
  float2 lo;
  float2 hi;
};

// Vector target filled from mixed vector+vector pack elements.
template <typename... Ts>
float4 PackVectorMixed(Ts... vals) {
  float4 v = { vals... };
  return v;
}
float4 ManualVectorMixed(float2 a, float2 b) {
  float4 v = { a, b };
  return v;
}

// A flat initializer list overflowing across the boundary of an array of
// vectors.
template <typename... Ts>
float2 PackArrayOverflow(Ts... vals) {
  float2 arr[2] = { vals... };
  return arr[0] + arr[1];
}
float2 ManualArrayOverflow(float a, float b, float c, float d) {
  float2 arr[2] = { a, b, c, d };
  return arr[0] + arr[1];
}

// Struct-member scalarization, including a vector-typed member.
template <typename... Ts>
PairOf2 PackStruct(Ts... vals) {
  PairOf2 s = { vals... };
  return s;
}
PairOf2 ManualStruct(float2 a, float2 b) {
  PairOf2 s = { a, b };
  return s;
}

// CHECK: define void @main()
// CHECK-DAG: fmul fast float %{{.*}}, 2.000000e+00
// CHECK-DAG: fmul fast float %{{.*}}, 6.000000e+00
// CHECK-DAG: fmul fast float %{{.*}}, 4.000000e+00
// CHECK-DAG: fmul fast float %{{.*}}, 4.000000e+00
// CHECK: call void @dx.op.storeOutput.f32(i32 5, i32 0, i32 0, i8 0
// CHECK: call void @dx.op.storeOutput.f32(i32 5, i32 0, i32 0, i8 1
// CHECK: call void @dx.op.storeOutput.f32(i32 5, i32 0, i32 0, i8 2
// CHECK: call void @dx.op.storeOutput.f32(i32 5, i32 0, i32 0, i8 3
float4 main(float4 inp : A) : SV_Target {
  float2 lo = inp.xy;
  float2 hi = inp.zw;

  float4 vp = PackVectorMixed(lo, hi);
  float4 vm = ManualVectorMixed(lo, hi);

  float2 op = PackArrayOverflow(inp.x, inp.y, inp.z, inp.w);
  float2 om = ManualArrayOverflow(inp.x, inp.y, inp.z, inp.w);

  PairOf2 sp = PackStruct(lo, hi);
  PairOf2 sm = ManualStruct(lo, hi);

  return vp + vm + float4(op + om, 0, 0) +
         float4(sp.lo + sm.lo, sp.hi + sm.hi);
}

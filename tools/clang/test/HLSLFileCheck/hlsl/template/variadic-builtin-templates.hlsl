// RUN: %dxc -E main -T ps_6_0 -HV 202x %s | FileCheck %s

// End-to-end coverage of HLSL 202x variadic templates interacting with
// HLSL's own built-in class templates (`vector<T,N>`, `matrix<T,R,C>`) and
// built-in resource templates (`StructuredBuffer<T>`), not just ordinary
// user-defined class/function templates. These are the constructs Clang's
// upstream C++ test suite has no notion of (it doesn't know about HLSL
// built-in templates or resource types at all), so passing that suite
// gives zero confidence that pack expansion actually reaches these paths
// correctly in DXC's fork.

template <typename T>
struct Holder {
  StructuredBuffer<T> Buf;
};
Holder<float> g_Holder : register(t0);

// A parameter pack's length is forwarded into the dimension argument of
// the built-in `vector<T, N>` template.
template <typename T, typename... Rest>
vector<T, 1 + sizeof...(Rest)> MakeVector(T First, Rest... Others) {
  return vector<T, 1 + sizeof...(Rest)>(First, Others...);
}

// A wrapper template around the built-in `matrix<T, R, C>` template,
// instantiated alongside variadic-template code in the same shader.
template <typename T, int R, int C>
struct MatrixWrapper {
  matrix<T, R, C> M;
};

// CHECK: call %dx.types.Handle @dx.op.createHandle(i32 57
// CHECK: call %dx.types.ResRet.f32 @dx.op.bufferLoad.f32
// CHECK: call void @dx.op.storeOutput.f32(i32 5, i32 0, i32 0, i8 0
// CHECK: call void @dx.op.storeOutput.f32(i32 5, i32 0, i32 0, i8 1
// CHECK: call void @dx.op.storeOutput.f32(i32 5, i32 0, i32 0, i8 2
// CHECK: call void @dx.op.storeOutput.f32(i32 5, i32 0, i32 0, i8 3
float4 main(float4 a : A) : SV_Target {
  vector<float, 4> v = MakeVector(a.x, a.y, a.z, a.w);

  MatrixWrapper<float, 2, 2> mw;
  mw.M = matrix<float, 2, 2>(v.x, v.y, v.z, v.w);

  float bufVal = g_Holder.Buf.Load(0);

  return float4(mw.M._11, mw.M._22, bufVal, v.w);
}

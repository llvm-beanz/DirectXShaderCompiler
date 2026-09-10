// RUN: %dxr -generate-differentials %s | FileCheck %s
// RUN: %dxr -generate-differentials %s > %t.gen.hlsl
// RUN: cat %t.gen.hlsl %S/Inputs/no_diff_parameter_caller.hlsl > %t.run.hlsl
// RUN: %dxc -T cs_6_9 -E testMain -HV 2021 -Fo %t.dxil %t.run.hlsl
// RUN: %dxc -dumpbin %t.dxil | FileCheck %s --check-prefix=EXEC

// Parameter-level no_diff keeps x in the primal computation but prevents an
// x_expr reference from entering the backward expression graph.

// CHECK: namespace user { namespace ad { namespace bwd {
// CHECK: float f(inout GradientContext<float> context, Variable<float> x, Variable<float> y)
// CHECK: VariableExpr<float> x_expr = makeVariableExpr<float>(x);
// CHECK: VariableExpr<float> y_expr = makeVariableExpr<float>(y);
// CHECK: return compute_gradients(context, multiply<float>(x.value, y_expr));
// CHECK: } } } // namespace user::ad::bwd

// At x=2 and y=3: f=6, inactive dx=0, and active dy=x=2.
// EXEC: rawBufferStore.f32{{.*}}i32 0, i32 0, float 6.000000e+00
// EXEC: rawBufferStore.f32{{.*}}i32 1, i32 0, float 0.000000e+00
// EXEC: rawBufferStore.f32{{.*}}i32 2, i32 0, float 2.000000e+00

[[dxc::autodiff(bwd)]]
float f([[dxc::no_diff]] float x, float y) {
  return x * y;
}

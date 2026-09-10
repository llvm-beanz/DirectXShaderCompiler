// RUN: %dxr -generate-differentials %s | FileCheck %s
// RUN: %dxr -generate-differentials %s > %t.gen.hlsl
// RUN: cat %t.gen.hlsl %S/Inputs/compound_assign_bwd_caller.hlsl > %t.run.hlsl
// RUN: %dxc -T cs_6_9 -E testMain -HV 2021 -Fo %t.dxil %t.run.hlsl
// RUN: %dxc -dumpbin %t.dxil | FileCheck %s --check-prefix=EXEC
//
// [[dxc::no_diff]] applied to a sub-expression that calls a non-differentiable
// intrinsic (asuint). Without the attribute, the backward-mode rewriter would
// mark the whole function non-differentiable and emit a `_Static_assert`
// stub. The attribute lets the user explicitly tell the rewriter to keep
// the offending sub-expression as-is and proceed with the rest of the
// translation.

// CHECK: namespace user { namespace ad { namespace bwd {
// CHECK: float f(inout GradientContext<float> context, Variable<float> x)
// CHECK: VariableExpr<float> x_expr = makeVariableExpr<float>(x);
// CHECK: return compute_gradients(context, add<float>(x_expr, (float)asuint(x.value)));
// CHECK-NOT: _Static_assert
// CHECK: } } } // namespace user::ad::bwd

// The bit-cast contributes to the primal but is inactive, so df/dx is 1.
// EXEC: rawBufferStore.f32{{.*}}i32 1, i32 0, float 1.000000e+00

[[dxc::autodiff(bwd)]]
float f(float x) {
  return x + [[dxc::no_diff]] (float)asuint(x);
}

float main(float x : A) : SV_Target { return f(x); }

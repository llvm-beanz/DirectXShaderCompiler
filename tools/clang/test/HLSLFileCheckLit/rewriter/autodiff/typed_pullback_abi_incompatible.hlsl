// RUN: %dxr -generate-differentials %s | FileCheck %s

// The current expression-template runtime uses one GradientContext<ResultType>
// for the whole graph. An active leaf with another type is rejected explicitly
// until the runtime can dispatch cotangents to heterogeneous contexts.

// CHECK: float f(inout GradientContext<float> context, Variable<float2> value, float __dxc_ad_seed)
// CHECK: _Static_assert(false, "auto-diff cannot generate backward-mode for 'f': active parameter 'value' has type 'float2', but the backward runtime requires active parameter types to match result type 'float'");

[[dxc::autodiff(bwd)]]
float f(float2 value) {
  return value.x;
}

// RUN: %dxr -generate-differentials %s | FileCheck %s

// Direct reverse lowering supports a result type that differs from one active
// leaf type. Multiple distinct active leaf types still require separate
// cotangent storage and are rejected explicitly.

// CHECK: float f(inout GradientContext<float2> context, Variable<float2> value, Variable<float> scale, float __dxc_ad_seed)
// CHECK: _Static_assert(false, "auto-diff cannot generate backward-mode for 'f': the backward runtime currently supports only one active parameter type per function");

[[dxc::autodiff(bwd)]]
float f(float2 value, float scale) {
  return value.x * scale;
}

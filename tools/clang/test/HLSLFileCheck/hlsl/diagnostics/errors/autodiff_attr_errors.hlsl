// RUN: %dxc -T ps_6_0 -E main %s | FileCheck %s

// Diagnostic tests for [[dxc::autodiff(...)]]:
//   * must have 1 or 2 arguments
//   * arguments must be the identifiers 'fwd' or 'bwd'
//   * may only be applied to functions

// CHECK-DAG: 'autodiff' attribute takes at least 1 argument
[[dxc::autodiff()]]
float f1(float x) { return x; }

// CHECK-DAG: 'autodiff' attribute requires parameter 1 to be an identifier
[[dxc::autodiff(foo)]]
float f2(float x) { return x; }

// CHECK-DAG: 'autodiff' attribute takes no more than 2 arguments
[[dxc::autodiff(fwd, bwd, fwd)]]
float f3(float x) { return x; }

// CHECK-DAG: 'autodiff' attribute only applies to functions
[[dxc::autodiff(fwd)]]
static int g = 0;

float main(float x : A) : SV_Target { return x; }

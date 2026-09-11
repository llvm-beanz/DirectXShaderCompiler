// RUN: %dxr -generate-differentials %s | FileCheck %s

// Runtime-dependent loops require reverse control-flow state and are rejected
// until the typed statement IR can represent a loop tape.

// CHECK: _Static_assert(false, "auto-diff cannot generate backward-mode for 'f': data-dependent control flow (for) is not differentiable");

[[dxc::autodiff(bwd)]]
float f(float value, [[dxc::no_diff]] uint count) {
  for (uint iteration = 0; iteration < count; ++iteration)
    value *= 2.0f;
  return value;
}

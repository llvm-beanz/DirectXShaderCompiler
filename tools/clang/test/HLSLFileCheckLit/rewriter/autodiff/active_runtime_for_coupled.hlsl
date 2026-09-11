// RUN: %dxr -generate-differentials %s | FileCheck %s

// Cross-target updates require a per-iteration Jacobian and are not part of
// independent multi-carried recurrence lowering.

// CHECK: _Static_assert(false, "auto-diff cannot generate backward-mode for 'f': active multi-carried runtime loop updates must be independent");

[[dxc::autodiff(bwd)]]
float f(float x, float y, [[dxc::no_diff]] uint count) {
  for (uint iteration = 0; iteration < count; ++iteration) {
    x += y;
    y *= 2.0f;
  }
  return x + y;
}

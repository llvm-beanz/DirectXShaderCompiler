// RUN: %dxr -generate-differentials %s | FileCheck %s

// Multiplying one carried value by another requires both primal trajectories
// to reconstruct the per-iteration Jacobian.

// CHECK: _Static_assert(false, "auto-diff cannot generate backward-mode for 'f': active multi-carried runtime loop updates must be independent");

[[dxc::autodiff(bwd)]]
float f(float x, float y, [[dxc::no_diff]] uint count) {
  for (uint iteration = 0; iteration < count; ++iteration) {
    x *= y;
    y += 1.0f;
  }
  return x + y;
}
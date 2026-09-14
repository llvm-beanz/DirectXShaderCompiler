// RUN: %dxr -generate-differentials %s | FileCheck %s

// Mixed monomials require both changing primal trajectories, so repeated active
// factors still require statically bounded tapes.

// CHECK: _Static_assert(false, "auto-diff cannot generate backward-mode for 'f': active nonlinear runtime chain requires a min(count, N) bound with N between 1 and 1024");

[[dxc::autodiff(bwd)]]
float f(float x, float y, float z, [[dxc::no_diff]] uint count) {
  for (uint iteration = 0; iteration < count; ++iteration) {
    x = x * y * y;
    y += z;
    z *= 2.0f;
  }
  return x + y + z;
}

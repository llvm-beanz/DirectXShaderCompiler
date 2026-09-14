// RUN: %dxr -generate-differentials %s | FileCheck %s

// An inactive scale changes the local Jacobian but not the requirement to bound
// both product operand tapes.

// CHECK: _Static_assert(false, "auto-diff cannot generate backward-mode for 'f': active nonlinear runtime chain requires a min(count, N) bound with N between 1 and 1024");

[[dxc::autodiff(bwd)]]
float f(float x, float y, float z, [[dxc::no_diff]] uint count,
        [[dxc::no_diff]] float scale) {
  for (uint iteration = 0; iteration < count; ++iteration) {
    x = y * (scale * x);
    y += z;
    z *= 2.0f;
  }
  return x + y + z;
}

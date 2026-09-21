// RUN: %dxr -generate-differentials %s | FileCheck %s

// Exponential, logarithmic, and root factors still require bounded storage for
// changing carried primal trajectories.

// CHECK: _Static_assert(false, "auto-diff cannot generate backward-mode for 'f': active runtime loop pullback requires a min(count, N) bound with N between 1 and 1024");

[[dxc::autodiff(bwd)]]
float f(float x, float y, float z, [[dxc::no_diff]] uint count) {
  for (uint iteration = 0; iteration < count; ++iteration) {
    x = exp(x) * y + log(x) * y - sqrt(x) * y;
    y += z;
    z *= 2.0f;
  }
  return x + y + z;
}

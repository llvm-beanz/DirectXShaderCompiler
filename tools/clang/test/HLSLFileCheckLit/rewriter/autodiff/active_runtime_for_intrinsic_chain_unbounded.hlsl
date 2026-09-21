// RUN: %dxr -generate-differentials %s | FileCheck %s

// Non-polynomial target factors still need bounded storage for the changing
// target and peer trajectories.

// CHECK: _Static_assert(false, "auto-diff cannot generate backward-mode for 'f': active runtime loop pullback requires a min(count, N) bound with N between 1 and 1024");

[[dxc::autodiff(bwd)]]
float f(float x, float y, float z, [[dxc::no_diff]] uint count) {
  for (uint iteration = 0; iteration < count; ++iteration) {
    x = sin(x) * y - cos(x) * y;
    y += z;
    z *= 2.0f;
  }
  return x + y + z;
}

// RUN: %dxr -generate-differentials %s | FileCheck %s

// Both changing primal trajectories require bounded storage.

// CHECK: _Static_assert(false, "auto-diff cannot generate backward-mode for 'f': active nonlinear coupled runtime loop requires a min(count, N) bound with N between 1 and 1024");

[[dxc::autodiff(bwd)]]
float f(float x, float y, [[dxc::no_diff]] uint count) {
  for (uint iteration = 0; iteration < count; ++iteration) {
    x *= y;
    y += 1.0f;
  }
  return x + y;
}
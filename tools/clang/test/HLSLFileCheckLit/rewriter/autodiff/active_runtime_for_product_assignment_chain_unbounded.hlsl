// RUN: %dxr -generate-differentials %s | FileCheck %s

// Assignment-form products need both changing primal trajectories, so their
// affine suffix does not remove the requirement for statically bounded tapes.

// CHECK: _Static_assert(false, "auto-diff cannot generate backward-mode for 'f': active runtime loop pullback requires a min(count, N) bound with N between 1 and 1024");

[[dxc::autodiff(bwd)]]
float f(float x, float y, float z, [[dxc::no_diff]] uint count,
        [[dxc::no_diff]] float bias) {
  for (uint iteration = 0; iteration < count; ++iteration) {
    x = y * x - bias;
    y += z;
    z *= 2.0f;
  }
  return x + y + z;
}

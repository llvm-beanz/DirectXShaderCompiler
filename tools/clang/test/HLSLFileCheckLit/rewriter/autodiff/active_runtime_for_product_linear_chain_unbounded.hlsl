// RUN: %dxr -generate-differentials %s | FileCheck %s

// Adding an inactive-scaled target term does not remove the need to bound both
// changing product operand tapes.

// CHECK: _Static_assert(false, "auto-diff cannot generate backward-mode for 'f': active runtime loop pullback requires a min(count, N) bound with N between 1 and 1024");

[[dxc::autodiff(bwd)]]
float f(float x, float y, float z, [[dxc::no_diff]] uint count,
        [[dxc::no_diff]] float scale, [[dxc::no_diff]] float slope) {
  for (uint iteration = 0; iteration < count; ++iteration) {
    x = scale * x * y - slope * x;
    y += z;
    z *= 2.0f;
  }
  return x + y + z;
}

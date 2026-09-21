// RUN: %dxr -generate-differentials %s | FileCheck %s

// Linear terms on both product operands still require bounded primal tapes for
// the changing product partials.

// CHECK: _Static_assert(false, "auto-diff cannot generate backward-mode for 'f': active runtime loop pullback requires a min(count, N) bound with N between 1 and 1024");

[[dxc::autodiff(bwd)]]
float f(float x, float y, float z, [[dxc::no_diff]] uint count,
        [[dxc::no_diff]] float targetSlope,
        [[dxc::no_diff]] float peerSlope) {
  for (uint iteration = 0; iteration < count; ++iteration) {
    x = x * y - targetSlope * x - peerSlope * y;
    y += z;
    z *= 2.0f;
  }
  return x + y + z;
}

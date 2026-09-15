// RUN: %dxr -generate-differentials %s | FileCheck %s

// A live post-update state version needs statically bounded storage just like
// iteration-input primals.

// CHECK: _Static_assert(false, "auto-diff cannot generate backward-mode for 'f': active generic runtime loop requires a min(count, N) bound with N between 1 and 1024");

[[dxc::autodiff(bwd)]]
float f(float x, float y, float z, [[dxc::no_diff]] uint count) {
  for (uint iteration = 0; iteration < count; ++iteration) {
    x += y;
    y = x * z;
    z *= 2.0f;
  }
  return x + y + z;
}

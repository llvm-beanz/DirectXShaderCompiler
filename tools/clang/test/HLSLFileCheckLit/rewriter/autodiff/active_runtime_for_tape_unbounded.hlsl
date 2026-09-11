// RUN: %dxr -generate-differentials %s | FileCheck %s

// Nonlinear recurrence storage must have a statically proven bound; an
// arbitrary runtime count cannot index a fixed local tape safely.

// CHECK: _Static_assert(false, "auto-diff cannot generate backward-mode for 'f': nonlinear active runtime loop requires a min(count, N) bound with N between 1 and 1024");

[[dxc::autodiff(bwd)]]
float f(float value, [[dxc::no_diff]] uint count) {
  for (uint iteration = 0; iteration < count; ++iteration)
    value *= value;
  return value;
}
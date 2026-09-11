// RUN: %dxr -generate-differentials %s | FileCheck %s

// The compact reverse replay supports only loop-invariant derivatives. A
// factor that references the induction variable requires saved iteration state.

// CHECK: _Static_assert(false, "auto-diff cannot generate backward-mode for 'f': active runtime loop factor must be loop-invariant");

[[dxc::autodiff(bwd)]]
float f(float value, [[dxc::no_diff]] uint count) {
  for (uint iteration = 0; iteration < count; ++iteration)
    value *= (float)(iteration + 1);
  return value;
}
// RUN: %dxc -E main -T ps_6_0 -HV 202x %s | FileCheck %s
// CHECK: call void @dx.op.storeOutput.f32(i32 5, i32 0, i32 0, i8 0, float 1.000000e+01)

// Verify that C++-like variadic templates (template parameter packs,
// function parameter packs, pack expansions, and sizeof...()) compile
// end-to-end down to DXIL in HLSL 202x.

template <typename T>
T Sum(T First) {
  return First;
}

template <typename T, typename U, typename... Rest>
T Sum(T First, U Second, Rest... Others) {
  return First + Sum(Second, Others...);
}

template <typename... Args>
uint CountArgs(Args... args) {
  return sizeof...(Args);
}

float main() : SV_Target {
  // 1 + 2 + 3 + 4 == 10, folded at compile time.
  float total = Sum(1.0, 2.0, 3.0, 4.0);
  // Adds 0 since CountArgs(1,2,3) == 3 and we only want to keep `total` as
  // the interesting value being checked; multiply by 0 to avoid altering it
  // while still forcing CountArgs/sizeof... to be codegen'd.
  total += 0 * (float)CountArgs(1, 2, 3);
  return total;
}

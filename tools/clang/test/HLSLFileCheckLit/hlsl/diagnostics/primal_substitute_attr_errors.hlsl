// RUN: not %dxc -T lib_6_4 %s 2>&1 | FileCheck %s

float not_a_function;

// CHECK-DAG: primal_substitute_of attribute argument must name a function
[[dxc::primal_substitute_of(not_a_function)]]
[[dxc::autodiff(bwd)]]
float bad_name(float x) { return x; }

// CHECK-DAG: primal_substitute_of attribute argument must name a function
[[dxc::primal_substitute_of(unknown_primal)]]
[[dxc::autodiff(bwd)]]
float missing_name(float x) { return x; }

float return_original(float x) { return x; }

// CHECK-DAG: primal substitute 'return_substitute' must return 'float'
[[dxc::primal_substitute_of(return_original)]]
[[dxc::autodiff(bwd)]]
float2 return_substitute(float x) { return x.xx; }

float count_original(float x, float y) { return x + y; }

// CHECK-DAG: primal substitute 'count_substitute' requires 2 parameters but has 1
[[dxc::primal_substitute_of(count_original)]]
[[dxc::autodiff(bwd)]]
float count_substitute(float x) { return x; }

float type_original(float x) { return x; }

// CHECK-DAG: primal substitute 'type_substitute' parameter 1 must have type 'float'
[[dxc::primal_substitute_of(type_original)]]
[[dxc::autodiff(bwd)]]
float type_substitute(float2 x) { return x.x; }

float activity_original([[dxc::no_diff]] float scale, float x) {
  return scale * x;
}

// CHECK-DAG: primal substitute 'activity_substitute' parameter 1 must have the same no_diff annotation as the primal
[[dxc::primal_substitute_of(activity_original)]]
[[dxc::autodiff(bwd)]]
float activity_substitute(float scale, float x) { return scale * x; }

float direction_original(out float x) {
  x = 1.0f;
  return x;
}

// CHECK-DAG: primal substitute 'direction_substitute' parameter 1 must have the same direction as the primal
[[dxc::primal_substitute_of(direction_original)]]
[[dxc::autodiff(bwd)]]
float direction_substitute(float x) { return x; }

float mode_original(float x) { return x; }

// CHECK-DAG: primal substitute 'mode_substitute' must request backward-mode autodiff or provide a backward derivative
[[dxc::primal_substitute_of(mode_original)]]
float mode_substitute(float x) { return x; }

float duplicate_original(float x) { return x; }

[[dxc::primal_substitute_of(duplicate_original)]]
[[dxc::autodiff(bwd)]]
float duplicate_one(float x) { return x; }

// CHECK-DAG: function 'duplicate_original' has multiple primal substitutes
[[dxc::primal_substitute_of(duplicate_original)]]
[[dxc::autodiff(bwd)]]
float duplicate_two(float x) { return x; }

// CHECK-DAG: function 'self_substitute' cannot substitute for itself
[[dxc::primal_substitute_of(self_substitute)]]
[[dxc::autodiff(bwd)]]
float self_substitute(float x) { return x; }

// CHECK-DAG: primal substitute association for '{{cycle_a|cycle_b}}' forms a cycle
[[dxc::primal_substitute_of(cycle_b)]]
[[dxc::autodiff(bwd)]]
float cycle_a(float x) { return x; }

[[dxc::primal_substitute_of(cycle_a)]]
[[dxc::autodiff(bwd)]]
float cycle_b(float x) { return x; }

void overloaded_original(float2 x) {}
void overloaded_original(float3 x) {}

// CHECK-DAG: no overload of primal 'overloaded_original' matches substitute 'overload_substitute'
[[dxc::primal_substitute_of(overloaded_original)]]
[[dxc::autodiff(bwd)]]
void overload_substitute(float x) {}

struct MethodMismatch {
  static float original(float x) { return x; }

  // CHECK-DAG: primal substitute 'substitute' must match the primal's method receiver and staticness
  [[dxc::primal_substitute_of(original)]]
  [[dxc::autodiff(bwd)]]
  float substitute(float x) { return x; }
};
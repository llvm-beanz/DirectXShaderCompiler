// RUN: %dxr -generate-differentials %s | FileCheck %s

// Forward-mode autodiff on a member function generates a wrapper struct
// `user::ad::fwd::Sphere : ::Sphere` that exposes a Value<T>-based
// counterpart for every annotated method, while leaving the original
// class definition untouched.

// CHECK: #include <ad/fwd>
// CHECK: struct Sphere
// CHECK: namespace user { namespace ad { namespace fwd {
// CHECK: using namespace ::ad::fwd;
// CHECK: struct Sphere : ::Sphere {
// CHECK: Value<float> area(Value<float> k)
// CHECK: ((this.radius * k) * k)
// CHECK: }
// CHECK: } } } // namespace user::ad::fwd

struct Sphere {
  float radius;
  [[dxc::autodiff(fwd)]]
  float area(float k) {
    return radius * k * k;
  }
};

float main(float x : A) : SV_Target {
  Sphere s;
  s.radius = 1.0;
  return s.area(x);
}

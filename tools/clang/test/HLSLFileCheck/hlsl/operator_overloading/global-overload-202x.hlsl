// RUN: %dxc -T lib_6_6 -HV 202x %s | FileCheck %s

// Verify that HLSL 202x global (non-member) operator overloads are selected
// during code generation for both class and enumeration operands, for binary
// and unary expressions.

struct S {
  float v;
};

S operator+(S a, S b) {
  S r;
  r.v = a.v + b.v;
  return r;
}

S operator-(S a) {
  S r;
  r.v = -a.v;
  return r;
}

bool operator==(S a, S b) { return a.v == b.v; }

enum E { A = 1, B = 2 };
int operator+(E a, E b) { return (int)a + (int)b; }
int operator-(E a) { return -(int)a; }

// CHECK: define {{.*}} @"\01?test_struct{{.*}}"
export float test_struct(float x, float y) {
  S a = { x };
  S b = { y };
  // CHECK: fcmp {{(fast )?}}oeq float %x, %y
  // CHECK: fsub {{(fast )?}}float -0
  S c = a + b;
  S d = -c;
  bool eq = (a == b);
  return d.v + (eq ? 0.0f : 1.0f);
}

// CHECK: define {{.*}} @"\01?test_enum{{.*}}"
export int test_enum() {
  // CHECK: ret i32 2
  return (A + B) + (-A);
}

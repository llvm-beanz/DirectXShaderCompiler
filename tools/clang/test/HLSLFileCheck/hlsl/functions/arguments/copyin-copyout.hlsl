// RUN: %dxc -T lib_6_4 -fcgl %s | FileCheck %s

void CalledFunction(inout float X, inout float Y, inout float Z) {
  X = 1.0;
  Y = 2.0;
  Z = 3.0;
}

void fn() {
  float X, Y, Z = 0.0;
  CalledFunction(X, Y, Z);

  CalledFunction(X, X, Z);

  CalledFunction(X, Y, X);
}

// Each out/inout argument materializes its own copy-in/copy-out temporary.
// Aliasing is not exploited at the AST level; the IR optimizer is expected
// to elide redundant copies after inlining.

// CHECK: define internal void @"\01?fn{{[@$?.A-Za-z0-9_]+}}"()
// CHECK: [[X:%[0-9A-Z]+]] = alloca float, align 4
// CHECK: [[Y:%[0-9A-Z]+]] = alloca float, align 4
// CHECK: [[Z:%[0-9A-Z]+]] = alloca float, align 4
// CHECK: [[T1A:%[0-9a-z.]+]] = alloca float
// CHECK: [[T1B:%[0-9a-z.]+]] = alloca float
// CHECK: [[T1C:%[0-9a-z.]+]] = alloca float
// CHECK: [[T2A:%[0-9a-z.]+]] = alloca float
// CHECK: [[T2B:%[0-9a-z.]+]] = alloca float
// CHECK: [[T2C:%[0-9a-z.]+]] = alloca float
// CHECK: [[T3A:%[0-9a-z.]+]] = alloca float
// CHECK: [[T3B:%[0-9a-z.]+]] = alloca float
// CHECK: [[T3C:%[0-9a-z.]+]] = alloca float

// First call: CalledFunction(X, Y, Z) - copies for all three params.
// CHECK: load float, float* [[Z]]
// CHECK: store float {{.*}}, float* [[T1A]]
// CHECK: load float, float* [[Y]]
// CHECK: store float {{.*}}, float* [[T1B]]
// CHECK: load float, float* [[X]]
// CHECK: store float {{.*}}, float* [[T1C]]
// CHECK: call void @"\01?CalledFunction
// CHECK-SAME: (float* dereferenceable(4) [[T1C]], float* dereferenceable(4) [[T1B]], float* dereferenceable(4) [[T1A]])
// CHECK: load float, float* [[T1C]]
// CHECK: store float {{.*}}, float* [[X]]
// CHECK: load float, float* [[T1B]]
// CHECK: store float {{.*}}, float* [[Y]]
// CHECK: load float, float* [[T1A]]
// CHECK: store float {{.*}}, float* [[Z]]

// Second call: CalledFunction(X, X, Z).
// CHECK: load float, float* [[Z]]
// CHECK: store float {{.*}}, float* [[T2A]]
// CHECK: load float, float* [[X]]
// CHECK: store float {{.*}}, float* [[T2B]]
// CHECK: load float, float* [[X]]
// CHECK: store float {{.*}}, float* [[T2C]]
// CHECK: call void @"\01?CalledFunction
// CHECK-SAME: (float* dereferenceable(4) [[T2C]], float* dereferenceable(4) [[T2B]], float* dereferenceable(4) [[T2A]])
// CHECK: load float, float* [[T2C]]
// CHECK: store float {{.*}}, float* [[X]]
// CHECK: load float, float* [[T2B]]
// CHECK: store float {{.*}}, float* [[X]]
// CHECK: load float, float* [[T2A]]
// CHECK: store float {{.*}}, float* [[Z]]

// Third call: CalledFunction(X, Y, X).
// CHECK: load float, float* [[X]]
// CHECK: store float {{.*}}, float* [[T3A]]
// CHECK: load float, float* [[Y]]
// CHECK: store float {{.*}}, float* [[T3B]]
// CHECK: load float, float* [[X]]
// CHECK: store float {{.*}}, float* [[T3C]]
// CHECK: call void @"\01?CalledFunction
// CHECK-SAME: (float* dereferenceable(4) [[T3C]], float* dereferenceable(4) [[T3B]], float* dereferenceable(4) [[T3A]])
// CHECK: load float, float* [[T3C]]
// CHECK: store float {{.*}}, float* [[X]]
// CHECK: load float, float* [[T3B]]
// CHECK: store float {{.*}}, float* [[Y]]
// CHECK: load float, float* [[T3A]]
// CHECK: store float {{.*}}, float* [[X]]

// CHECK: ret

void fn2() {
  float X = 0.0;
  CalledFunction(X, X, X);
}

// CHECK: define internal void @"\01?fn2
// CHECK: [[X:%[0-9A-Z]+]] = alloca float, align 4
// CHECK: [[F2A:%[0-9a-z.]+]] = alloca float
// CHECK: [[F2B:%[0-9a-z.]+]] = alloca float
// CHECK: [[F2C:%[0-9a-z.]+]] = alloca float

// All three parameters get their own temporary; right-to-left store order.
// CHECK: load float, float* [[X]]
// CHECK: store float {{.*}}, float* [[F2A]]
// CHECK: load float, float* [[X]]
// CHECK: store float {{.*}}, float* [[F2B]]
// CHECK: load float, float* [[X]]
// CHECK: store float {{.*}}, float* [[F2C]]

// CHECK: call void @"\01?CalledFunction
// CHECK-SAME: (float* dereferenceable(4) [[F2C]], float* dereferenceable(4) [[F2B]], float* dereferenceable(4) [[F2A]])

// Writebacks in left-to-right order; X is overwritten by each one.
// CHECK: load float, float* [[F2C]]
// CHECK: store float {{.*}}, float* [[X]]
// CHECK: load float, float* [[F2B]]
// CHECK: store float {{.*}}, float* [[X]]
// CHECK: load float, float* [[F2A]]
// CHECK: store float {{.*}}, float* [[X]]

// CHECK: ret

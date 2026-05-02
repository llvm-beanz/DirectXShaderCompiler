// RUN: %dxc -T lib_6_4 -fcgl -HV 2021 %s | FileCheck %s

struct Doggo {
void operator()(inout float X, inout int Y, inout float Z) {
  X = 1.0;
  Y = 2;
  Z = 3.0;
}
};

void fn() {
  float X, Z = 0.0;
  int Y = 0;
  Doggo D;
  D(X, Y, Z);

  D(X, Y, X);
}

// Each out/inout argument materializes its own copy-in/copy-out temporary.
// Aliasing is not exploited at the AST level; the IR optimizer is expected
// to elide redundant copies after inlining.

// CHECK: define internal void @"\01?fn{{[@$?.A-Za-z0-9_]+}}"()
// CHECK: [[X:%[0-9A-Z]+]] = alloca float, align 4
// CHECK: [[Z:%[0-9A-Z]+]] = alloca float, align 4
// CHECK: [[Y:%[0-9A-Z]+]] = alloca i32, align 4
// CHECK: [[D:%[0-9A-Z]+]] = alloca %struct.Doggo
// CHECK: [[T1A:%[0-9a-z.]+]] = alloca float
// CHECK: [[T1B:%[0-9a-z.]+]] = alloca i32
// CHECK: [[T1C:%[0-9a-z.]+]] = alloca float
// CHECK: [[T2A:%[0-9a-z.]+]] = alloca float
// CHECK: [[T2B:%[0-9a-z.]+]] = alloca i32
// CHECK: [[T2C:%[0-9a-z.]+]] = alloca float

// First call D(X, Y, Z) - all three args copied right-to-left.
// CHECK: load float, float* [[Z]]
// CHECK: store float {{.*}}, float* [[T1A]]
// CHECK: load i32, i32* [[Y]]
// CHECK: store i32 {{.*}}, i32* [[T1B]]
// CHECK: load float, float* [[X]]
// CHECK: store float {{.*}}, float* [[T1C]]
// CHECK: call void @"\01??RDoggo{{[@$?.A-Za-z0-9_]+}}"(%struct.Doggo* [[D]], float* dereferenceable(4) [[T1C]], i32* dereferenceable(4) [[T1B]], float* dereferenceable(4) [[T1A]])
// CHECK: load float, float* [[T1C]]
// CHECK: store float {{.*}}, float* [[X]]
// CHECK: load i32, i32* [[T1B]]
// CHECK: store i32 {{.*}}, i32* [[Y]]
// CHECK: load float, float* [[T1A]]
// CHECK: store float {{.*}}, float* [[Z]]

// Second call D(X, Y, X).
// CHECK: load float, float* [[X]]
// CHECK: store float {{.*}}, float* [[T2A]]
// CHECK: load i32, i32* [[Y]]
// CHECK: store i32 {{.*}}, i32* [[T2B]]
// CHECK: load float, float* [[X]]
// CHECK: store float {{.*}}, float* [[T2C]]
// CHECK: call void @"\01??RDoggo{{[@$?.A-Za-z0-9_]+}}"(%struct.Doggo* [[D]], float* dereferenceable(4) [[T2C]], i32* dereferenceable(4) [[T2B]], float* dereferenceable(4) [[T2A]])
// CHECK: load float, float* [[T2C]]
// CHECK: store float {{.*}}, float* [[X]]
// CHECK: load i32, i32* [[T2B]]
// CHECK: store i32 {{.*}}, i32* [[Y]]
// CHECK: load float, float* [[T2A]]
// CHECK: store float {{.*}}, float* [[X]]

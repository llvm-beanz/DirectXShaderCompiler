// RUN: %dxc -T lib_6_4 -fcgl %s | FileCheck %s


struct Pup {
  float X;
};

void CalledFunction(inout float F, inout Pup P) {
  F = 4.0;
  P.X = 5.0;
}

void fn() {
  float X;
  Pup P;

  CalledFunction(X, P);
  CalledFunction(P.X, P);
}

// CHECK: define internal void @"\01?fn@
// CHECK-DAG: [[P:%[0-9A-Z]+]] = alloca %struct.Pup
// CHECK-DAG: [[X:%[0-9A-Z]+]] = alloca float, align 4
// CHECK-DAG: [[TmpX:%[0-9a-z.]+]] = alloca float

// CHECK: call void @"\01?CalledFunction{{[@$?.A-Za-z0-9_]+}}"(float* dereferenceable(4) [[X]], %struct.Pup*  dereferenceable(4) [[P]])

// CHECK: [[PXPtr:%[0-9A-Z]+]] = getelementptr inbounds %struct.Pup, %struct.Pup* [[P]], i32 0, i32 0
// CHECK: [[PXVal:%[0-9A-Z]+]] = load float, float* [[PXPtr]], align 4
// CHECK: store float [[PXVal]], float* [[TmpX]]

// CHECK-DAG: call void @"\01?CalledFunction{{[@$?.A-Za-z0-9_]+}}"(float* dereferenceable(4) [[TmpX]], %struct.Pup*  dereferenceable(4) [[P]])
// CHECK-DAG: [[PXPtr2:%[0-9A-Z]+]] = getelementptr inbounds %struct.Pup, %struct.Pup* [[P]], i32 0, i32 0

// CHECK: [[TmpVal:%[0-9A-Z]+]] = load float, float* [[TmpX]]
// CHECK: store float [[TmpVal]], float* [[PXPtr2]], align 4

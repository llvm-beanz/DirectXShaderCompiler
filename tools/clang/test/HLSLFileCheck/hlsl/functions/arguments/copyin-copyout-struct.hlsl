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
// Each inout argument now materializes its own temporary; verify the
// structural copy-in / call / writeback pattern without binding the
// individual temporaries (their numbering is fragile).
// CHECK-DAG: alloca float, align 4
// CHECK-DAG: alloca %struct.Pup
// CHECK-DAG: alloca %struct.Pup
// CHECK-DAG: alloca float

// First call: copy-in P, copy-in X, call.
// CHECK: bitcast %struct.Pup*
// CHECK: bitcast %struct.Pup*
// CHECK: call void @llvm.memcpy.p0i8.p0i8.i64(
// CHECK: load float, float*
// CHECK: store float
// CHECK: call void @"\01?CalledFunction{{[@$?.A-Za-z0-9_]+}}"(float* dereferenceable(4) %{{[0-9]+}}, %struct.Pup* dereferenceable(4) %{{[0-9]+}})

// First writeback: load TmpX, store back to X; memcpy P from TmpP.
// CHECK: load float, float*
// CHECK: store float
// CHECK: call void @llvm.memcpy.p0i8.p0i8.i64(

// Second call: copy-in P, copy-in P.X, call.
// CHECK: bitcast %struct.Pup*
// CHECK: bitcast %struct.Pup*
// CHECK: call void @llvm.memcpy.p0i8.p0i8.i64(
// CHECK: getelementptr inbounds %struct.Pup, %struct.Pup*
// CHECK: load float, float*
// CHECK: store float
// CHECK: call void @"\01?CalledFunction{{[@$?.A-Za-z0-9_]+}}"(float* dereferenceable(4) %{{[0-9]+}}, %struct.Pup* dereferenceable(4) %{{[0-9]+}})


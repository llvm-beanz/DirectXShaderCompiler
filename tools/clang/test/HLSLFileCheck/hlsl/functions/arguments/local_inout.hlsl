// RUN: %dxc -E main -Tps_6_0 -fcgl %s | FileCheck %s


// Each inout array argument materializes a copy-in/copy-out temporary at the
// AST level: main allocates the original array plus one temp for foo and one
// for bar. The IR optimizer is expected to elide these copies after inlining.
// CHECK: define float @main(
// CHECK: alloca [5 x i32]
// CHECK: alloca [5 x i32]
// CHECK: alloca [5 x i32]
// CHECK-NOT: alloca [5 x i32]
// CHECK: ret float

void foo(inout uint a[5], uint b) {
    a[0] = b;
    a[1] = b+1;
    a[2] = b+2;
    a[3] = b+3;
    a[4] = b+4;
}

uint bar(inout uint a[5], uint i) {
   return a[i];
}

float main(uint2 i:A) : SV_Target {
  uint a[5];
  foo(a, i.x);
  return bar(a, i.y);
}
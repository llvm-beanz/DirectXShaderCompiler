// RUN: %dxc -E main -Tps_6_0 -fcgl %s | FileCheck %s


// Each inout array argument materializes a copy-in/copy-out temporary, so
// main allocates the original array plus one temp for the call to bar, and
// bar allocates a temp for the nested call to foo. The IR optimizer is
// expected to elide these copies after inlining.
// CHECK: define float @main(
// CHECK: alloca [5 x i32]
// CHECK: alloca [5 x i32]
// CHECK-NOT: alloca [5 x i32]
// CHECK: define internal i32 @"\01?bar
// CHECK: alloca [5 x i32]
// CHECK-NOT: alloca [5 x i32]

void foo(inout uint a[5], uint b) {
    a[0] = b;
    a[1] = b+1;
    a[2] = b+2;
    a[3] = b+3;
    a[4] = b+4;
}

uint bar(inout uint a[5], uint2 i) {
  foo(a, i.x);
  return a[i.y];
}

float main(uint2 i:A) : SV_Target {
  uint a[5];
  return bar(a, i);
}
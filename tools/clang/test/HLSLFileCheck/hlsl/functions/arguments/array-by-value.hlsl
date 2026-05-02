// RUN: %dxc -T vs_6_0 -fcgl %s | FileCheck %s

// Test that array arguments are passed by value (copy semantics).
// The array is copied into a temporary before the call, and changes inside
// the function do not affect the caller's array.

void fn(float x[2]) { }

float main(float val: A) : B {
  float Arr[2] = {0, 0};
  fn(Arr);
  return Arr[0];
}

// CHECK: define float @main(float %val)
// CHECK: %Arr = alloca [2 x float]
// CHECK: %[[TMP:[0-9]+]] = alloca [2 x float]

// The array Arr is copied into a temporary before the call
// CHECK: call void @llvm.memcpy{{.*}}(i8* %{{[0-9]+}}, i8* %{{[0-9]+}}, i64 8
// CHECK: call void @{{.*fn.*}}([2 x float]* %[[TMP]])

// The original Arr is unmodified after the call
// CHECK: %[[PTR:[0-9]+]] = getelementptr inbounds [2 x float], [2 x float]* %Arr, i32 0, i32 0
// CHECK: %[[RET:[0-9]+]] = load float, float* %[[PTR]]
// CHECK: ret float %[[RET]]

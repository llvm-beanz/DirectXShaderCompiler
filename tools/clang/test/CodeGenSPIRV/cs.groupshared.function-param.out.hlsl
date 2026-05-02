// RUN: %dxc -T cs_6_0 -E main -fcgl  %s -spirv | FileCheck %s

struct S {
  int a;
  float b;
};

void foo(out int x, out int y, out int z, out S w, out int v) {
  x = 1;
  y = x << 1;
  z = x << 2;
  w.a = x << 3;
  v = x << 4;
}

// CHECK: %A = OpVariable %_ptr_Uniform_type_RWStructuredBuffer_int Uniform
RWStructuredBuffer<int> A;

// CHECK: %B = OpVariable %_ptr_Private_int Private
static int B;

// CHECK: %C = OpVariable %_ptr_Workgroup_int Workgroup
groupshared int C;

// CHECK: %D = OpVariable %_ptr_Workgroup_S Workgroup
groupshared S D;

[numthreads(1,1,1)]
void main() {
// CHECK: %E = OpVariable %_ptr_Function_int Function
  int E;

// CHECK:      {{%[0-9]+}} = OpFunctionCall %void %foo %hlsl_out %hlsl_out_0 %hlsl_out_1 %hlsl_out_2 %hlsl_out_3
// CHECK:      [[Ld0:%[0-9]+]] = OpLoad %int %hlsl_out
// CHECK-NEXT: [[Ac0:%[0-9]+]] = OpAccessChain %_ptr_Uniform_int %A %int_0 %uint_0
// CHECK-NEXT:                   OpStore [[Ac0]] [[Ld0]]
// CHECK-NEXT: [[Ld1:%[0-9]+]] = OpLoad %int %hlsl_out_0
// CHECK-NEXT:                   OpStore %B [[Ld1]]
// CHECK-NEXT: [[Ld2:%[0-9]+]] = OpLoad %int %hlsl_out_1
// CHECK-NEXT:                   OpStore %C [[Ld2]]
// CHECK-NEXT: [[Ld3:%[0-9]+]] = OpLoad %S %hlsl_out_2
// CHECK-NEXT:                   OpStore %D [[Ld3]]
// CHECK-NEXT: [[Ld4:%[0-9]+]] = OpLoad %int %hlsl_out_3
// CHECK-NEXT:                   OpStore %E [[Ld4]]
  foo(A[0], B, C, D, E);
  A[0] = A[0] | B | C | D.a | E;
}

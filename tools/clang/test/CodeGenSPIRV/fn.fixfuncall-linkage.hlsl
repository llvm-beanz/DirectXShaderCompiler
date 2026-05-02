// RUN: %dxc -T lib_6_3 -fspv-target-env=universal1.5 -fspv-fix-func-call-arguments -O0  %s -spirv | FileCheck %s

// CHECK-DAG: OpCapability Shader
// CHECK-DAG: OpCapability Linkage
RWStructuredBuffer< float4 > output : register(u1);

// CHECK: OpDecorate %main LinkageAttributes "main" Export
// CHECK: %main = OpFunction %int None
// CHECK: [[s36:%[a-zA-Z0-9_]+]] = OpVariable %_ptr_Function_float Function
// CHECK: [[s39:%[a-zA-Z0-9_]+]] = OpVariable %_ptr_Function_int Function
// CHECK: [[s33:%[a-zA-Z0-9_]+]] = OpAccessChain %_ptr_StorageBuffer_float {{%[a-zA-Z0-9_]+}} %int_0
// CHECK: [[s37:%[a-zA-Z0-9_]+]] = OpLoad %float [[s33]]
// CHECK:                OpStore [[s36]] [[s37]]
// CHECK: {{%[a-zA-Z0-9_]+}} = OpFunctionCall %void %func [[s36]] [[s39]]
// CHECK: [[s38:%[a-zA-Z0-9_]+]] = OpLoad %float [[s36]]
// CHECK:                OpStore {{%[a-zA-Z0-9_]+}} [[s38]]

[noinline]
void func(inout float f0, inout int f1) {
  
}

struct Stru {
  int x;
  int y;
};
         
export int main(inout float4 color) {
  output[0] = color;
  Stru stru;
  func(output[0].x, stru.y);
  return 1;
}

// RUN: %dxc -T cs_6_0 -E main -fspv-fix-func-call-arguments -O0  %s -spirv | FileCheck %s
RWStructuredBuffer< float4 > output : register(u1);

[noinline]
float4 foo(inout float f0, inout int f1)
{
    return 0;
}

// CHECK: [[s36:%[a-zA-Z0-9_]+]] = OpVariable %_ptr_Function_float Function
// CHECK: [[s39:%[a-zA-Z0-9_]+]] = OpVariable %_ptr_Function_int Function
// CHECK: [[s33:%[a-zA-Z0-9_]+]] = OpAccessChain %_ptr_Uniform_float {{%[a-zA-Z0-9_]+}} %int_0
// CHECK: [[s37:%[a-zA-Z0-9_]+]] = OpLoad %float [[s33]]
// CHECK:                OpStore [[s36]] [[s37]]
// CHECK: {{%[a-zA-Z0-9_]+}} = OpFunctionCall %v4float %foo [[s36]] [[s39]]
// CHECK: [[s38:%[a-zA-Z0-9_]+]] = OpLoad %float [[s36]]
// CHECK:                OpStore {{%[a-zA-Z0-9_]+}} [[s38]]

struct Stru {
  int x;
  int y;
};

[numthreads(1,1,1)]
void main()
{
    Stru stru;
    foo(output[0].x, stru.y);
}

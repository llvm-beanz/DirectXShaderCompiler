// RUN: %dxc -T ps_6_2 -E main -enable-16bit-types -fcgl  %s -spirv | FileCheck %s
void foo(const half3 input, out half3 output) {
  output = input;
}

void bar( inout float3 p)
{
  p += float3(1,1,1);
}


float4 main() : SV_Target0 {
  float3 output;
// CHECK:       %param_var_input = OpVariable %_ptr_Function_v3half Function
// CHECK-NEXT:      %hlsl_out = OpVariable %_ptr_Function_v3half Function

// CHECK:                              OpStore %param_var_input {{%[0-9]+}}
// CHECK-NEXT:              {{%[0-9]+}} = OpFunctionCall %void %foo %param_var_input %hlsl_out
  foo(float3(1, 0, 0), output);
// CHECK-NEXT:  [[outputHalf3_0:%[0-9]+]] = OpLoad %v3half %hlsl_out
// CHECK-NEXT: [[outputFloat3_0:%[0-9]+]] = OpFConvert %v3float [[outputHalf3_0]]
// CHECK-NEXT:                         OpStore %output [[outputFloat3_0]]

// CHECK:      [[f1:%[0-9]+]] = OpLoad %float %f
// CHECK-NEXT: [[f2:%[0-9]+]] = OpLoad %float %f
// CHECK-NEXT: [[f3:%[0-9]+]] = OpLoad %float %f
// CHECK-NEXT: [[splat:%[0-9]+]] = OpCompositeConstruct %v3float [[f1]] [[f2]] [[f3]]
// CHECK-NEXT:               OpStore %p3 [[splat]]
// CHECK-NEXT: [[p3_ld:%[0-9]+]] = OpLoad %v3float %p3
// CHECK-NEXT:               OpStore %temp_var_hlsl_inout [[p3_ld]]
// CHECK-NEXT: OpFunctionCall %void %bar %temp_var_hlsl_inout
// CHECK-NEXT: [[ret:%[0-9]+]] = OpLoad %v3float %temp_var_hlsl_inout
// CHECK-NEXT:               OpStore %p3 [[ret]]
   float f = 0;
   float3 p3 = float3(f, f, f);
   bar(p3);

// CHECK: [[outputFloat3_1:%[0-9]+]] = OpLoad %v3float %output
// CHECK-NEXT: OpCompositeExtract %float [[outputFloat3_2:%[0-9]+]] 0
// CHECK-NEXT: OpCompositeExtract %float [[outputFloat3_3:%[0-9]+]] 1
// CHECK-NEXT: OpCompositeExtract %float [[outputFloat3_4:%[0-9]+]] 2
// CHECK-NEXT: OpCompositeConstruct %v4float
  return float4(output, 1.0f);
}


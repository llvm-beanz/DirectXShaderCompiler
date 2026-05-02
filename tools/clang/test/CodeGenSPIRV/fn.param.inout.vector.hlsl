// RUN: %dxc -T vs_6_0 -E main -fcgl  %s -spirv | FileCheck %s

RWBuffer<float4>    MyRWBuffer;
RWTexture2D<float3> MyRWTexture;

void foo(inout float4 a, out float3 b);
void bar(inout float4 x, out float3 y, inout float2 z, out float w);

float4 main() : C {
// CHECK: %temp_var_hlsl_inout = OpVariable %_ptr_Function_v4float Function
// CHECK: %hlsl_out = OpVariable %_ptr_Function_v3float Function
// CHECK: %val = OpVariable %_ptr_Function_v4float Function
// CHECK: %temp_var_hlsl_inout_0 = OpVariable %_ptr_Function_v4float Function
// CHECK: %hlsl_out_0 = OpVariable %_ptr_Function_v3float Function
// CHECK: %temp_var_hlsl_inout_1 = OpVariable %_ptr_Function_v2float Function
// CHECK: %hlsl_out_1 = OpVariable %_ptr_Function_float Function

// CHECK: [[buf_rd:%[0-9]+]] = OpImageRead %v4float {{%[0-9]+}} %uint_5 None
// CHECK:                OpStore %temp_var_hlsl_inout [[buf_rd]]
// CHECK:           {{%[0-9]+}} = OpFunctionCall %void %foo %temp_var_hlsl_inout %hlsl_out
// CHECK-NEXT:   [[a:%[0-9]+]] = OpLoad %v4float %temp_var_hlsl_inout
// CHECK-NEXT: [[buf:%[0-9]+]] = OpLoad %type_buffer_image %MyRWBuffer
// CHECK-NEXT:                OpImageWrite [[buf]] %uint_5 [[a]] None
// CHECK-NEXT:   [[b:%[0-9]+]] = OpLoad %v3float %hlsl_out
// CHECK-NEXT: [[tex:%[0-9]+]] = OpLoad %type_2d_image %MyRWTexture
// CHECK-NEXT:                OpImageWrite [[tex]] {{%[0-9]+}} [[b]] None
    foo(MyRWBuffer[5], MyRWTexture[uint2(6, 7)]);

    float4 val;
// CHECK:    [[val0:%[0-9]+]] = OpLoad %v4float %val
// CHECK:                OpStore %temp_var_hlsl_inout_0 [[val0]]
// CHECK:    [[val1:%[0-9]+]] = OpLoad %v4float %val
// CHECK:    [[z_sh:%[0-9]+]] = OpVectorShuffle %v2float [[val1]] [[val1]] 0 1
// CHECK:                OpStore %temp_var_hlsl_inout_1 [[z_sh]]
// CHECK:           {{%[0-9]+}} = OpFunctionCall %void %bar %temp_var_hlsl_inout_0 %hlsl_out_0 %temp_var_hlsl_inout_1 %hlsl_out_1
// CHECK-NEXT:  [[x_wb:%[0-9]+]] = OpLoad %v4float %temp_var_hlsl_inout_0
// CHECK-NEXT:                OpStore %val [[x_wb]]
// CHECK-NEXT:   [[y:%[0-9]+]] = OpLoad %v3float %hlsl_out_0
// CHECK-NEXT: [[old:%[0-9]+]] = OpLoad %v4float %val
    // Write to val.zwx:
    //   val[2] = out_val[0] => 0 + 4 = 4
    //   val[3] = out_val[1] => 1 + 4 = 5
    //   val[0] = out_val[2] => 2 + 4 = 6
    //   val[1] = val[1]     => 1 + 0 = 1
// CHECK-NEXT: [[new:%[0-9]+]] = OpVectorShuffle %v4float [[old]] [[y]] 6 1 4 5
// CHECK-NEXT:                OpStore %val [[new]]
    // Write to val.xy:
    //   val[0] = out_val[0] => 0 + 4 = 4
    //   val[1] = out_val[1] => 1 + 4 = 5
    //   val[2] = val[2]     => 2 + 0 = 2
    //   val[3] = val[3]     => 3 + 0 = 3
// CHECK-NEXT:   [[z:%[0-9]+]] = OpLoad %v2float %temp_var_hlsl_inout_1
// CHECK-NEXT: [[old_0:%[0-9]+]] = OpLoad %v4float %val
// CHECK-NEXT: [[new_0:%[0-9]+]] = OpVectorShuffle %v4float [[old_0]] [[z]] 4 5 2 3
// CHECK-NEXT:                OpStore %val [[new_0]]
// CHECK-NEXT:   [[w:%[0-9]+]] = OpLoad %float %hlsl_out_1
// CHECK-NEXT: [[z_ptr:%[0-9]+]] = OpAccessChain %_ptr_Function_float %val %int_2
// CHECK-NEXT:                OpStore [[z_ptr]] [[w]]
    bar(val, val.zwx, val.xy, val.z);

    return MyRWBuffer[0];
}

void foo(inout float4 a, out float3 b) {
    a = 4.2;
    b = 2.4;
}

void bar(inout float4 x, out float3 y, inout float2 z, out float w) {
    x = 1.1;
    y = 2.2;
    z = 3.3;
    w = 4.4;
}

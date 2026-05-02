// RUN: %dxc -T vs_6_0 -E main -fcgl  %s -spirv | FileCheck %s

struct Pixel {
  float4 color;
};

float fnInOut(uniform float a, in float b, out float c, inout float d, inout Pixel e) {
    float v = a + b + c + d;
    d = c = a;
    return v;
}

float main(float val: A) : B {
    float m, n;
    Pixel p;

// CHECK:      %param_var_a = OpVariable %_ptr_Function_float Function
// CHECK-NEXT: %param_var_b = OpVariable %_ptr_Function_float Function
// CHECK-NEXT:   %hlsl_out = OpVariable %_ptr_Function_float Function
// CHECK-NEXT: %temp_var_hlsl_inout = OpVariable %_ptr_Function_float Function
// CHECK-NEXT: %temp_var_hlsl_inout_0 = OpVariable %_ptr_Function_Pixel Function

// CHECK-NEXT:                OpStore %param_var_a %float_5
// CHECK-NEXT: [[val:%[0-9]+]] = OpLoad %float %val
// CHECK-NEXT:                OpStore %param_var_b [[val]]
// CHECK-NEXT: [[n:%[0-9]+]] = OpLoad %float %n
// CHECK-NEXT:                OpStore %temp_var_hlsl_inout [[n]]
// CHECK-NEXT: [[p:%[0-9]+]] = OpLoad %Pixel %p
// CHECK-NEXT:                OpStore %temp_var_hlsl_inout_0 [[p]]

// CHECK-NEXT: [[ret:%[0-9]+]] = OpFunctionCall %float %fnInOut %param_var_a %param_var_b %hlsl_out %temp_var_hlsl_inout %temp_var_hlsl_inout_0
// CHECK-NEXT: [[m_ld:%[0-9]+]] = OpLoad %float %hlsl_out
// CHECK-NEXT:                OpStore %m [[m_ld]]
// CHECK-NEXT: [[n_ld:%[0-9]+]] = OpLoad %float %temp_var_hlsl_inout
// CHECK-NEXT:                OpStore %n [[n_ld]]
// CHECK-NEXT: [[p_ld:%[0-9]+]] = OpLoad %Pixel %temp_var_hlsl_inout_0
// CHECK-NEXT:                OpStore %p [[p_ld]]

// CHECK-NEXT:                OpReturnValue [[ret]]
    return fnInOut(5., val, m, n, p);
}

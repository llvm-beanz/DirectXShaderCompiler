// RUN: %dxc -T vs_6_0 -E main -fcgl  %s -spirv | FileCheck %s

RWStructuredBuffer<float> Data;

void foo(in float a, inout float b, out float c) {
    b += a;
    c = a + b;
}

void main(float input : INPUT) {
// CHECK: %param_var_a = OpVariable %_ptr_Function_float Function
// CHECK: %temp_var_hlsl_inout = OpVariable %_ptr_Function_float Function
// CHECK: %hlsl_out = OpVariable %_ptr_Function_float Function

// CHECK: [[val:%[0-9]+]] = OpLoad %float %input
// CHECK:                OpStore %param_var_a [[val]]
// CHECK:  [[p0:%[0-9]+]] = OpAccessChain %_ptr_Uniform_float %Data %int_0 %uint_0
// CHECK:  [[ld0:%[0-9]+]] = OpLoad %float [[p0]]
// CHECK:                OpStore %temp_var_hlsl_inout [[ld0]]

// CHECK:                OpFunctionCall %void %foo %param_var_a %temp_var_hlsl_inout %hlsl_out
// CHECK: [[wb0:%[0-9]+]] = OpLoad %float %temp_var_hlsl_inout
// CHECK:  [[q0:%[0-9]+]] = OpAccessChain %_ptr_Uniform_float %Data %int_0 %uint_0
// CHECK:                OpStore [[q0]] [[wb0]]
// CHECK: [[wb1:%[0-9]+]] = OpLoad %float %hlsl_out
// CHECK:  [[q1:%[0-9]+]] = OpAccessChain %_ptr_Uniform_float %Data %int_0 %uint_1
// CHECK:                OpStore [[q1]] [[wb1]]
    foo(input, Data[0], Data[1]);
}

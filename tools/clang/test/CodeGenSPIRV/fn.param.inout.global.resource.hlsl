// RUN: %dxc -E main -T ps_6_0 -fspv-target-env=vulkan1.2 -fcgl  %s -spirv | FileCheck %s

Texture2D<float4>               r0;
RWTexture3D<float4>             r1;
SamplerState                    r2;
RaytracingAccelerationStructure r3;
RWBuffer<float4>                r4;
ByteAddressBuffer               r5;
RWByteAddressBuffer             r6;
RWStructuredBuffer<float4>      r7;
AppendStructuredBuffer<float4>  r8;

float4 run(inout    Texture2D<float4>               a0,
           inout    RWTexture3D<float4>             a1,
           inout    SamplerState                    a2,
           inout    RaytracingAccelerationStructure a3,
           inout    RWBuffer<float4>                a4,
           inout    ByteAddressBuffer               a5,
           inout    RWByteAddressBuffer             a6,
           inout    RWStructuredBuffer<float4>      a7,
           inout    AppendStructuredBuffer<float4>  a8)
{
    float4 pos = a4.Load(0);
    return a0.Sample(a2, float2(a6.Load(pos.x), a5.Load(pos.y)));
}

float4 main(): SV_Target
{
// For each inout argument, a temporary variable (hlsl.inout) is created in the
// caller.  Non-buffer resources (images, samplers, RTAS, buffer images) are
// loaded into the temporary; struct-based buffer types (ByteAddressBuffer, etc.)
// are stored as pointer aliases without a load (no OpStore type mismatch).

// CHECK: %temp_var_hlsl_inout   = OpVariable %_ptr_Function_type_2d_image Function
// CHECK: %temp_var_hlsl_inout_0 = OpVariable %_ptr_Function_type_3d_image Function
// CHECK: %temp_var_hlsl_inout_1 = OpVariable %_ptr_Function_type_sampler Function
// CHECK: %temp_var_hlsl_inout_2 = OpVariable %_ptr_Function_accelerationStructureNV Function
// CHECK: %temp_var_hlsl_inout_3 = OpVariable %_ptr_Function_type_buffer_image Function
// CHECK: %temp_var_hlsl_inout_4 = OpVariable %_ptr_Function__ptr_StorageBuffer_type_ByteAddressBuffer Function
// CHECK: %temp_var_hlsl_inout_5 = OpVariable %_ptr_Function__ptr_StorageBuffer_type_RWByteAddressBuffer Function
// CHECK: %temp_var_hlsl_inout_6 = OpVariable %_ptr_Function__ptr_StorageBuffer_type_RWStructuredBuffer_v4float Function
// CHECK: %temp_var_hlsl_inout_7 = OpVariable %_ptr_Function__ptr_StorageBuffer_type_AppendStructuredBuffer_v4float Function

// Non-buffer resources: load the resource value then store into the temp var.
// CHECK: [[r0:%[a-zA-Z0-9_]+]] = OpLoad %type_2d_image %r0
// CHECK:                         OpStore %temp_var_hlsl_inout [[r0]]
// CHECK: [[r1:%[a-zA-Z0-9_]+]] = OpLoad %type_3d_image %r1
// CHECK:                         OpStore %temp_var_hlsl_inout_0 [[r1]]
// CHECK: [[r2:%[a-zA-Z0-9_]+]] = OpLoad %type_sampler %r2
// CHECK:                         OpStore %temp_var_hlsl_inout_1 [[r2]]
// CHECK: [[r3:%[a-zA-Z0-9_]+]] = OpLoad %accelerationStructureNV %r3
// CHECK:                         OpStore %temp_var_hlsl_inout_2 [[r3]]
// CHECK: [[r4:%[a-zA-Z0-9_]+]] = OpLoad %type_buffer_image %r4
// CHECK:                         OpStore %temp_var_hlsl_inout_3 [[r4]]

// Struct-based buffer resources: store the StorageBuffer pointer directly into
// the Function alias variable (no intermediate load to avoid type mismatch).
// CHECK: OpStore %temp_var_hlsl_inout_4 %r5
// CHECK: OpStore %temp_var_hlsl_inout_5 %r6
// CHECK: OpStore %temp_var_hlsl_inout_6 %r7
// CHECK: OpStore %temp_var_hlsl_inout_7 %r8

// CHECK: OpFunctionCall %v4float %run %temp_var_hlsl_inout %temp_var_hlsl_inout_0 %temp_var_hlsl_inout_1 %temp_var_hlsl_inout_2 %temp_var_hlsl_inout_3 %temp_var_hlsl_inout_4 %temp_var_hlsl_inout_5 %temp_var_hlsl_inout_6 %temp_var_hlsl_inout_7

    return run(r0, r1, r2, r3, r4, r5, r6, r7, r8);
}

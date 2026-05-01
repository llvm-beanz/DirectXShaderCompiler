// RUN: %dxc -T ps_6_10 -E main -fcgl %s -spirv | FileCheck %s

// CHECK: OpCapability UniformAndStorageBuffer8BitAccess
// CHECK: OpCapability Int8

// CHECK: OpExtension "SPV_KHR_8bit_storage"

// CHECK: OpMemberDecorate %type_MyCBuffer 0 Offset 0
// CHECK: OpMemberDecorate %type_MyCBuffer 1 Offset 1
// CHECK: OpDecorate %type_MyCBuffer Block

cbuffer MyCBuffer {
    int8_t gVal1;   // 8-bit signed integer
    uint8_t gVal2;  // 8-bit unsigned integer
};

float4 main() : SV_Target {
    return float4(gVal1, gVal2, 0, 1);
}

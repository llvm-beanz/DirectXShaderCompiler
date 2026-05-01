// RUN: %dxc -T ps_6_10 -E main -fcgl %s -spirv -fspv-target-env=vulkan1.2 | FileCheck %s

// For Vulkan 1.2+, SPV_KHR_8bit_storage is promoted to core and should not
// be emitted as an extension, but the capabilities still must be present.

// CHECK: OpCapability UniformAndStorageBuffer8BitAccess
// CHECK: OpCapability Int8

// CHECK-NOT: OpExtension "SPV_KHR_8bit_storage"

// CHECK: OpDecorate %type_MyCBuffer Block

cbuffer MyCBuffer {
    int8_t gVal1;
    uint8_t gVal2;
};

float4 main() : SV_Target {
    return float4(gVal1, gVal2, 0, 1);
}

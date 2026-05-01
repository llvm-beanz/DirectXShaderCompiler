// RUN: %dxc -T ps_6_10 -E main -fcgl %s -spirv | FileCheck %s

// CHECK: OpCapability StoragePushConstant8
// CHECK: OpCapability Int8

// CHECK: OpExtension "SPV_KHR_8bit_storage"

// CHECK: OpMemberDecorate %type_PushConstant_S 0 Offset 0
// CHECK: OpMemberDecorate %type_PushConstant_S 1 Offset 1
// CHECK: OpMemberDecorate %type_PushConstant_S 2 Offset 4
// CHECK: OpDecorate %type_PushConstant_S Block

struct S {
    int8_t val1;   // 8-bit signed integer
    uint8_t val2;  // 8-bit unsigned integer
    float val3;
};

[[vk::push_constant]]
S MyPC;

float4 main() : SV_Target {
    return float4(MyPC.val1, MyPC.val2, MyPC.val3, 1);
}

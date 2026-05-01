// RUN: %dxc -T ps_6_10 -E main -fcgl %s -spirv | FileCheck %s

// CHECK: OpCapability StorageBuffer8BitAccess
// CHECK: OpCapability Int8

// CHECK: OpExtension "SPV_KHR_8bit_storage"

// CHECK: OpMemberDecorate %S 0 Offset 0
// CHECK: OpMemberDecorate %S 1 Offset 1
// CHECK: OpMemberDecorate %S 2 Offset 4

// CHECK: OpDecorate %_runtimearr_S ArrayStride 8

// CHECK: OpMemberDecorate %type_StructuredBuffer_S 0 Offset 0
// CHECK: OpMemberDecorate %type_StructuredBuffer_S 0 NonWritable

// CHECK: OpDecorate %type_StructuredBuffer_S BufferBlock

struct S {
    int8_t val1;   // 8-bit signed integer
    uint8_t val2;  // 8-bit unsigned integer
    float val3;
};

StructuredBuffer<S> MySBuffer;

float4 main() : SV_Target {
    return float4(MySBuffer[0].val1, MySBuffer[0].val2, MySBuffer[0].val3, 1);
}

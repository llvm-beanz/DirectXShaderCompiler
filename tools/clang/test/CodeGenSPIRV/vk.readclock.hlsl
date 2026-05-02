// RUN: %dxc -T vs_6_0 -spirv %s 2>&1 | FileCheck %s

// Test vk::ReadClock returns a 64-bit integer (ulong), which is then truncated
// to uint for the output. This exercises the Int64 and ShaderClockKHR
// capabilities.

// CHECK: OpCapability Int64
// CHECK: OpCapability ShaderClockKHR
// CHECK: OpExtension "SPV_KHR_shader_clock"

uint main() : A {
   return vk::ReadClock(vk::SubgroupScope);
}

// CHECK: %ulong = OpTypeInt 64 0
// CHECK: %[[RESULT:[0-9]+]] = OpReadClockKHR %ulong
// CHECK: %[[TRUNC:[0-9]+]] = OpUConvert %uint %[[RESULT]]
// CHECK: OpStore {{.*}} %[[TRUNC]]

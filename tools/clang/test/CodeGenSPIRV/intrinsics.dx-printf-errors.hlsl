// RUN: not %dxc -spirv -T cs_6_0 -E main -HV 202x %s 2>&1 | FileCheck %s
// RUN: not %dxc -spirv -fspv-target-env=vulkan1.1 -T cs_6_0 -E main -HV 202x %s 2>&1 | FileCheck %s

// CHECK: error: "dx::printf is only supported for DirectX targets"
// CHECK-NOT: intrinsic function unimplemented
// CHECK-NOT: LLVM ERROR

#include <dx/printf.h>

[numthreads(1, 1, 1)]
void main() {
  dx::InitializeDebugStream();
  dx::printf("hello");
}

// RUN: %dxc -T cs_6_0 -E main -HV 202x %s | FileCheck %s
// RUN: %dxc -T cs_6_0 -E main -HV 202x -Od %s | FileCheck %s

// CHECK-NOT: llvm.string.to.offset
// CHECK: call i32 @dx.op.atomicCompareExchange.i32
// CHECK: call i32 @dx.op.wavePrefixOp
// CHECK: call i32 @dx.op.waveActiveOp
// CHECK: call i32 @dx.op.atomicBinOp.i32
// CHECK: call void @dx.op.bufferStore.i32
// CHECK-NOT: llvm.string.to.offset

#include <dx/printf.h>
#include <dx/printf.h>

[numthreads(64, 1, 1)]
void main(uint Index : SV_GroupIndex) {
  dx::InitializeDebugStream();
  dx::printf("thread %u: %f", Index, float(Index));
  dx::printf("no arguments");
}

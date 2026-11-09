// RUN: %dxc -T cs_6_0 -E main -HV 202x %s | FileCheck %s
// RUN: %dxc -T cs_6_0 -E main -HV 202x -Od %s | FileCheck %s
// RUN: %dxc -T cs_6_0 -E main -HV 202x -Zi -Qembed_debug %s | FileCheck %s
// RUN: %dxc -T cs_6_0 -E main -HV 202x -Od -Zi -Qembed_debug %s | FileCheck %s
// RUN: %dxc -T cs_6_0 -E main -HV 202x -D ZERO_ARGS %s | FileCheck %s --check-prefix=ZERO
// RUN: %dxc -T cs_6_0 -E main -HV 202x -Od -D ZERO_ARGS %s | FileCheck %s --check-prefix=ZERO

// CHECK-LABEL: define void @main()
// CHECK: [[THREAD:%[-a-zA-Z$._0-9]+]] = call i32 @dx.op.flattenedThreadIdInGroup.i32
// CHECK: [[FORMAT:%[-a-zA-Z$._0-9]+]] = {{select i1 .*i32|phi i32}}
// CHECK: [[LANE:%[-a-zA-Z$._0-9]+]] = call i32 @dx.op.wavePrefixOp.i32
// CHECK: [[COUNT:%[-a-zA-Z$._0-9]+]] = call i32 @dx.op.waveActiveOp.i32
// CHECK: [[TOTAL:%[-a-zA-Z$._0-9]+]] = {{shl|mul}} i32 [[COUNT]], {{3|8}}
// CHECK: [[SIZE:%[-a-zA-Z$._0-9]+]] = add i32
// CHECK: call i32 @dx.op.atomicBinOp.i32({{.*}}i32 [[SIZE]])
// CHECK: call void @dx.op.bufferStore.i32(i32 69, {{.*}}, i32 [[PREFIX:%[-a-zA-Z$._0-9]+]], i32 undef, i32 [[COUNT]],
// CHECK: [[SIZE_OFFSET:%[-a-zA-Z$._0-9]+]] = add i32 [[PREFIX]], 4
// CHECK: call void @dx.op.bufferStore.i32(i32 69, {{.*}}, i32 [[SIZE_OFFSET]], i32 undef, i32 8,
// CHECK: call i32 @dx.op.waveReadLaneFirst.i32
// CHECK: {{shl|mul}} i32 [[LANE]], {{3|8}}
// CHECK: call void @dx.op.bufferStore.i32(i32 69, {{.*}}, i32 [[LANE_OFFSET:%[-a-zA-Z$._0-9]+]], i32 undef, i32 [[FORMAT]],
// CHECK: [[ARG_OFFSET:%[-a-zA-Z$._0-9]+]] = add i32 [[LANE_OFFSET]], 4
// CHECK: call void @dx.op.bufferStore.i32(i32 69, {{.*}}, i32 [[ARG_OFFSET]], i32 undef, i32 [[THREAD]],
// CHECK: ret void

// ZERO-LABEL: define void @main()
// ZERO: [[FORMAT:%[-a-zA-Z$._0-9]+]] = {{select i1 .*i32|phi i32}}
// ZERO: [[LANE:%[-a-zA-Z$._0-9]+]] = call i32 @dx.op.wavePrefixOp.i32
// ZERO: call i32 @dx.op.atomicBinOp.i32
// ZERO: call void @dx.op.bufferStore.i32
// ZERO: call void @dx.op.bufferStore.i32(i32 69, {{.*}}, i32 {{%[-a-zA-Z$._0-9]+}}, i32 undef, i32 4,
// ZERO: call i32 @dx.op.waveReadLaneFirst.i32
// ZERO: {{shl|mul}} i32 [[LANE]], {{2|4}}
// ZERO: call void @dx.op.bufferStore.i32(i32 69, {{.*}}, i32 {{%[-a-zA-Z$._0-9]+}}, i32 undef, i32 [[FORMAT]],
// ZERO-NOT: call void @dx.op.bufferStore.i32
// ZERO: ret void

#include <dx/printf.h>

[numthreads(64, 1, 1)]
void main(uint Index : SV_GroupIndex) {
#ifdef ZERO_ARGS
  string Format = "even\n";
  if (Index & 1)
    Format = "odd\n";
  dx::printf(Format);
#else
  string Format = "even %u\n";
  if (Index & 1)
    Format = "odd %u\n";
  dx::printf(Format, Index);
#endif
}

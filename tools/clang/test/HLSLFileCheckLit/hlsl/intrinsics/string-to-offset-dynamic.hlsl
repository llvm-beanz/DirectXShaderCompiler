// RUN: %dxc -T cs_6_0 -E main -HV 202x %s | FileCheck %s
// RUN: %dxc -T cs_6_0 -E main -HV 202x -Od %s | FileCheck %s
// RUN: %dxc -T cs_6_0 -E main -HV 202x -Zi -Qembed_debug %s | FileCheck %s
// RUN: %dxc -T cs_6_0 -E main -HV 202x -Od -Zi -Qembed_debug %s | FileCheck %s

// CHECK-NOT: {{^(.* = call|declare)}} i32 @llvm.string.to.offset
// CHECK-NOT: {{^ +%.* = phi i8\*}}
// CHECK-NOT: {{^ +%.* = select .*i8\*}}
// CHECK: call void @dx.op.bufferStore.i32
// CHECK: call void @dx.op.bufferStore.i32
// CHECK: call void @dx.op.bufferStore.i32
// CHECK: call void @dx.op.bufferStore.i32
// CHECK: call void @dx.op.bufferStore.i32
// CHECK-NOT: {{^(.* = call|declare)}} i32 @llvm.string.to.offset
// CHECK-NOT: {{^ +%.* = phi i8\*}}
// CHECK-NOT: {{^ +%.* = select .*i8\*}}
// CHECK: !{{[0-9]+}} = !{!"{{.*}}first\00{{.*}}"}

RWByteAddressBuffer Output : register(u0);

uint Offset(string Text) {
  return dx::__builtin_hlsl_string_to_offset(Text);
}

struct State {
  string Text;
};

[numthreads(64, 1, 1)]
void main(uint Index : SV_GroupIndex) {
  string Text = "first";
  if (Index & 1)
    Text = "second";
  Output.Store(0, Offset(Text));
  Output.Store(4, Offset(Text));
  State S = {Text};
  if (Index & 2)
    S.Text = "third";
  Output.Store(8, Offset(S.Text));

  for (uint I = 0; I < Index; ++I) {
    if (I & 1)
      Text = S.Text;
    else if (I & 2)
      Text = "first";
    Output.Store(16 + 4 * I, Offset(Text));
  }
  Output.Store(12, Offset(Text));
}

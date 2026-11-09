// RUN: %dxc -T cs_6_0 -E main -HV 202x %s | FileCheck %s
// RUN: %dxc -T cs_6_0 -E main -HV 202x -Od %s | FileCheck %s
// RUN: %dxc -T cs_6_0 -E main -HV 202x -Zi -Qembed_debug %s | FileCheck %s

// CHECK-NOT: {{^(.* = call|declare)}} i32 @llvm.string.to.offset
// CHECK: call void @dx.op.bufferStore.i32(i32 69, %dx.types.Handle {{.*}}, i32 0, i32 undef, i32 1,
// CHECK: call void @dx.op.bufferStore.i32(i32 69, %dx.types.Handle {{.*}}, i32 4, i32 undef, i32 1,
// CHECK: call void @dx.op.bufferStore.i32(i32 69, %dx.types.Handle {{.*}}, i32 8, i32 undef, i32 7,
// CHECK: call void @dx.op.bufferStore.i32(i32 69, %dx.types.Handle {{.*}}, i32 12, i32 undef, i32 1,
// CHECK: call void @dx.op.bufferStore.i32(i32 69, %dx.types.Handle {{.*}}, i32 16, i32 undef, i32 1,
// CHECK-NOT: {{^(.* = call|declare)}} i32 @llvm.string.to.offset

RWByteAddressBuffer Output : register(u0);

uint StringOffset(string Text) {
  return dx::__builtin_hlsl_string_to_offset(Text);
}

struct StringState {
  string Text;
};

struct NestedState {
  StringState Values[2];
};

struct DerivedState : StringState {
  uint Value;
};

[numthreads(1, 1, 1)]
void main() {
  Output.Store(0, StringOffset("hello"));
  Output.Store(4, StringOffset("hello"));
  Output.Store(8, StringOffset(""));
  string Text = "";
  Text = "hello";
  StringState Local = {Text};
  Local.Text = "hello";
  NestedState Nested = {{Local, Local}};
  DerivedState Derived;
  Derived.Text = Nested.Values[1].Text;
  Output.Store(12, StringOffset(Nested.Values[0].Text));
  Output.Store(16, StringOffset(Derived.Text));
}

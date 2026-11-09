// RUN: %dxc -T cs_6_0 -E main -HV 202x %s | FileCheck %s
// RUN: %dxc -T cs_6_0 -E main -HV 202x -Od %s | FileCheck %s
// RUN: %dxc -T cs_6_0 -E main -HV 202x -Zi -Qembed_debug %s | FileCheck %s
// RUN: %dxc -T cs_6_0 -E main -HV 2021 %s | FileCheck %s

// CHECK: call void @dx.op.bufferStore.i32(i32 69, %dx.types.Handle {{.*}}, i32 0, i32 undef, i32 1,
// CHECK: call void @dx.op.bufferStore.i32(i32 69, %dx.types.Handle {{.*}}, i32 4, i32 undef, i32 3,
// CHECK: call void @dx.op.bufferStore.i32(i32 69, %dx.types.Handle {{.*}}, i32 8, i32 undef, i32 1,
// CHECK: call void @dx.op.bufferStore.i32(i32 69, %dx.types.Handle {{.*}}, i32 12, i32 undef, i32 4,
// CHECK: call void @dx.op.bufferStore.i32(i32 69, %dx.types.Handle {{.*}}, i32 16, i32 undef, i32 1,
// CHECK: call void @dx.op.bufferStore.i32(i32 69, %dx.types.Handle {{.*}}, i32 20, i32 undef, i32 5,
// CHECK: call void @dx.op.bufferStore.i32(i32 69, %dx.types.Handle {{.*}}, i32 24, i32 undef, i32 1,
// CHECK: call void @dx.op.bufferStore.i32(i32 69, %dx.types.Handle {{.*}}, i32 28, i32 undef, i32 3,
// CHECK: call void @dx.op.bufferStore.i32(i32 69, %dx.types.Handle {{.*}}, i32 32, i32 undef, i32 1,
// CHECK: call void @dx.op.bufferStore.i32(i32 69, %dx.types.Handle {{.*}}, i32 36, i32 undef, i32 1,
// CHECK: call void @dx.op.bufferStore.i32(i32 69, %dx.types.Handle {{.*}}, i32 40, i32 undef, i32 6,
// CHECK: call void @dx.op.bufferStore.i32(i32 69, %dx.types.Handle {{.*}}, i32 44, i32 undef, i32 1,
// CHECK: call void @dx.op.bufferStore.i32(i32 69, %dx.types.Handle {{.*}}, i32 48, i32 undef, i32 1,
// CHECK: call void @dx.op.bufferStore.i32(i32 69, %dx.types.Handle {{.*}}, i32 52, i32 undef, i32 3,
// CHECK: call void @dx.op.bufferStore.i32(i32 69, %dx.types.Handle {{.*}}, i32 56, i32 undef, i32 1,
// CHECK: call void @dx.op.bufferStore.i32(i32 69, %dx.types.Handle {{.*}}, i32 60, i32 undef, i32 7,
// CHECK: !{{[0-9]+}} = !{!"\00hello\00\00"}

struct StringOnly {
  string Text;
};

struct StringState {
  string Text;
  uint Value;
};

typedef StringState StateType;

struct NestedState {
  StringState Values[2];
};

struct DerivedState : StringState {
  string Other;
};

static const string GlobalText = "hello";
static StringState GlobalLiteral = {"hello", 3};
static const StateType GlobalReference = {GlobalText, 4};
static NestedState Nested = {{{"hello", 5}, GlobalLiteral}};
static DerivedState Derived = {GlobalLiteral, GlobalText};
static const StringOnly OnlyText = {"hello"};

RWByteAddressBuffer Output : register(u0);

StringState Copy(StringState State) {
  return State;
}

void Set(out StringState State) {
  State.Text = "hello";
  State.Value = 6;
}

[numthreads(1, 1, 1)]
void main() {
  StringState Local = Copy(GlobalLiteral);
  Output.Store(0, dx::__builtin_hlsl_string_to_offset(Local.Text));
  Output.Store(4, Local.Value);
  Output.Store(8, dx::__builtin_hlsl_string_to_offset(GlobalReference.Text));
  Output.Store(12, GlobalReference.Value);
  Output.Store(16, dx::__builtin_hlsl_string_to_offset(Nested.Values[0].Text));
  Output.Store(20, Nested.Values[0].Value);
  Output.Store(24, dx::__builtin_hlsl_string_to_offset(Derived.Text));
  Output.Store(28, Derived.Value);
  Output.Store(32, dx::__builtin_hlsl_string_to_offset(Derived.Other));
  Set(Local);
  Output.Store(36, dx::__builtin_hlsl_string_to_offset(Local.Text));
  Output.Store(40, Local.Value);
  Output.Store(44, dx::__builtin_hlsl_string_to_offset(OnlyText.Text));
  Output.Store(48, dx::__builtin_hlsl_string_to_offset(Nested.Values[1].Text));
  Output.Store(52, Nested.Values[1].Value);
  static const StringState StaticLocal = {"hello", 7};
  Output.Store(56, dx::__builtin_hlsl_string_to_offset(StaticLocal.Text));
  Output.Store(60, StaticLocal.Value);
}

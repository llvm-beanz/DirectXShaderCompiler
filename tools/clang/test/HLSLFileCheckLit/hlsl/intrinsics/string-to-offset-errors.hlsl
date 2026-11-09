// RUN: not %dxc -T cs_6_0 -E main -HV 202x %s 2>&1 | FileCheck %s --check-prefix=DYNAMIC
// RUN: not %dxc -T cs_6_0 -E main -HV 202x -Od %s 2>&1 | FileCheck %s --check-prefix=DYNAMIC
// RUN: not %dxc -T cs_6_0 -E main -HV 202x -D MIXED_GRAPH %s 2>&1 | FileCheck %s --check-prefix=DYNAMIC
// RUN: not %dxc -T cs_6_0 -E main -HV 202x -Od -D MIXED_GRAPH %s 2>&1 | FileCheck %s --check-prefix=DYNAMIC
// RUN: not %dxc -T lib_6_3 -HV 202x -D CONSTANT_STRING %s 2>&1 | FileCheck %s --check-prefix=LIBRARY
// RUN: not %dxc -T lib_6_3 -HV 202x -Od -D CONSTANT_STRING %s 2>&1 | FileCheck %s --check-prefix=LIBRARY

// DYNAMIC: error: string-to-offset argument must resolve to constant strings through select or phi nodes
// DYNAMIC-NOT: LLVM ERROR
// LIBRARY: error: '__builtin_hlsl_string_to_offset' is unavailable: not supported in library shaders
// LIBRARY-NOT: LLVM ERROR

RWByteAddressBuffer Output : register(u0);

struct StringState {
  string Text;
};

[shader("compute")]
[numthreads(64, 1, 1)]
void main(uint Index : SV_GroupIndex) {
#ifdef CONSTANT_STRING
  string Text = "first";
#else
  StringState States[2] = {{"first"}, {"second"}};
  string Text = States[Index & 1].Text;
#ifdef MIXED_GRAPH
  if (Index & 2)
    Text = "third";
#endif
#endif
  Output.Store(0, dx::__builtin_hlsl_string_to_offset(Text));
}

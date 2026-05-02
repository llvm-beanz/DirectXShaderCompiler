// RUN: %dxc -T lib_6_x -fcgl %s | FileCheck %s

// Test that out parameters with struct types use a temporary alloca for the
// copy-out, which is then copied to the destination after the call.

struct Agg {
  float3 f3;
};

void get(out Agg agg);

static Agg s_agg;

export
float3 main() {
  get(s_agg);
  return s_agg.f3;
}

// An out parameter creates a temporary alloca, passes it to get(), then
// copies the result to the actual destination (s_agg).
// CHECK: define <3 x float> @{{.*main.*}}()
// CHECK: %[[TMP:[0-9]+]] = alloca %struct.Agg

// Call get() with the temporary
// CHECK: call void @{{.*get.*}}(%struct.Agg* dereferenceable(12) %[[TMP]])

// Copy the temporary result back to s_agg via memcpy (after bitcasting)
// CHECK: call void @llvm.memcpy

// Cleanup: lifetime.end for the temporary
// CHECK: call void @llvm.lifetime.end

// Return s_agg.f3
// CHECK: load <3 x float>, <3 x float>*
// CHECK: ret <3 x float>

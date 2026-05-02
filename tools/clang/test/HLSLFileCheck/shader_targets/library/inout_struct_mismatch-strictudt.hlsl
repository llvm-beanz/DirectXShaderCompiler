// RUN: %dxc -T lib_6_x -default-linkage external -HV 2021 %s | FileCheck %s

// With explicit copy-in/copy-out for inout aggregates, the cast from
// CallStruct to ParamStruct materializes a ParamStruct temporary and the
// fields are copied one by one before/after the call.
// CHECK: define <4 x float>
// CHECK-SAME: main
// CHECK: alloca %struct.ParamStruct
// CHECK: call void @"\01?modify_ext{{.*}}(%struct.ParamStruct* {{.*}}dereferenceable(8) %{{[0-9]+}})

struct ParamStruct {
  int i;
  float f;
};

struct CallStruct {
  int i;
  float f;
};

void modify(inout ParamStruct s) {
  s.f += 1;
}

void modify_ext(inout ParamStruct s);

CallStruct g_struct;

float4 main() : SV_Target {
  CallStruct local = g_struct;
  modify((ParamStruct)local);
  modify_ext((ParamStruct)local);
  return local.f;
}

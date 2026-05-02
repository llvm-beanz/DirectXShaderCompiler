// RUN: %dxc -T lib_6_x -default-linkage external -HV 2018 %s | FileCheck %s

// With out/inout parameter rewriting, calling modify_ext(local) on a
// CallStruct local now allocates a fresh ParamStruct temp and copies
// the fields in (and out) rather than reusing the CallStruct local
// via a struct-to-struct bitcast.
// CHECK: define <4 x float>
// CHECK-SAME: main
// CHECK: [[param:%[0-9]+]] = alloca %struct.ParamStruct
// CHECK: call void @"\01?modify_ext{{.*}}"(%struct.ParamStruct* {{.*}}dereferenceable(8) [[param]])

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
  modify(local);
  modify_ext(local);
  return local.f;
}

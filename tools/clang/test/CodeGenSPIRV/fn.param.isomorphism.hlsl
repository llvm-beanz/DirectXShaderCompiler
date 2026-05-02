// RUN: %dxc -T cs_6_0 -E main -fcgl  %s -spirv | FileCheck %s

struct R {
  int a;
  void incr() { ++a; }
};

// CHECK: %rwsb = OpVariable %_ptr_Uniform_type_RWStructuredBuffer_R Uniform
RWStructuredBuffer<R> rwsb;

struct S {
  int a;
  void incr() { ++a; }
};

// CHECK: %gs = OpVariable %_ptr_Workgroup_S Workgroup
groupshared S gs;

// CHECK: %st = OpVariable %_ptr_Private_S Private
static S st;

void decr(inout R foo) {
  foo.a--;
};

void decr2(inout S foo) {
  foo.a--;
};

void int_decr(out int foo) {
  ++foo;
}

// CHECK: %gsarr = OpVariable %_ptr_Workgroup__arr_S_uint_10 Workgroup
groupshared S gsarr[10];

// CHECK: %starr = OpVariable %_ptr_Private__arr_S_uint_10 Private
static S starr[10];

[numthreads(1, 1, 1)]
void main() {
// CHECK:    %fn = OpVariable %_ptr_Function_S Function
  S fn;

// CHECK: %fnarr = OpVariable %_ptr_Function__arr_S_uint_10 Function
  S fnarr[10];

// CHECK:   %arr = OpVariable %_ptr_Function__arr_int_uint_10 Function
  int arr[10];

// CHECK:      [[rwsb:%[0-9]+]] = OpAccessChain %_ptr_Uniform_R %rwsb %int_0 %uint_0
// CHECK-NEXT:      {{%[0-9]+}} = OpFunctionCall %void %R_incr [[rwsb]]
  rwsb[0].incr();

// CHECK: OpFunctionCall %void %S_incr %gs
  gs.incr();

// CHECK: OpFunctionCall %void %S_incr %st
  st.incr();

// CHECK: OpFunctionCall %void %S_incr %fn
  fn.incr();

// CHECK:      [[rwsb_0:%[0-9]+]] = OpAccessChain %_ptr_Uniform_R %rwsb %int_0 %uint_0
// CHECK-NEXT:              {{%[0-9]+}} = OpLoad %R [[rwsb_0]]
// CHECK:                               OpStore %temp_var_hlsl_inout {{%[0-9]+}}
// CHECK-NEXT:              {{%[0-9]+}} = OpFunctionCall %void %decr %temp_var_hlsl_inout
  decr(rwsb[0]);

// CHECK: [[gs_ld:%[0-9]+]] = OpLoad %S %gs
// CHECK-NEXT:                OpStore %temp_var_hlsl_inout_0 [[gs_ld]]
// CHECK-NEXT: {{%[0-9]+}} = OpFunctionCall %void %decr2 %temp_var_hlsl_inout_0
// CHECK-NEXT: [[gs_wb:%[0-9]+]] = OpLoad %S %temp_var_hlsl_inout_0
// CHECK-NEXT:                OpStore %gs [[gs_wb]]
  decr2(gs);

// CHECK: [[st_ld:%[0-9]+]] = OpLoad %S %st
// CHECK-NEXT:                OpStore %temp_var_hlsl_inout_1 [[st_ld]]
// CHECK-NEXT: {{%[0-9]+}} = OpFunctionCall %void %decr2 %temp_var_hlsl_inout_1
// CHECK-NEXT: [[st_wb:%[0-9]+]] = OpLoad %S %temp_var_hlsl_inout_1
// CHECK-NEXT:                OpStore %st [[st_wb]]
  decr2(st);

// CHECK: [[fn_ld:%[0-9]+]] = OpLoad %S %fn
// CHECK-NEXT:                OpStore %temp_var_hlsl_inout_2 [[fn_ld]]
// CHECK-NEXT: {{%[0-9]+}} = OpFunctionCall %void %decr2 %temp_var_hlsl_inout_2
// CHECK-NEXT: [[fn_wb:%[0-9]+]] = OpLoad %S %temp_var_hlsl_inout_2
// CHECK-NEXT:                OpStore %fn [[fn_wb]]
  decr2(fn);

// CHECK:      [[gsarr:%[0-9]+]] = OpAccessChain %_ptr_Workgroup_S %gsarr %int_0
// CHECK-NEXT:       {{%[0-9]+}} = OpFunctionCall %void %S_incr [[gsarr]]
  gsarr[0].incr();

// CHECK:      [[starr:%[0-9]+]] = OpAccessChain %_ptr_Private_S %starr %int_0
// CHECK-NEXT:       {{%[0-9]+}} = OpFunctionCall %void %S_incr [[starr]]
  starr[0].incr();

// CHECK:      [[fnarr:%[0-9]+]] = OpAccessChain %_ptr_Function_S %fnarr %int_0
// CHECK-NEXT:       {{%[0-9]+}} = OpFunctionCall %void %S_incr [[fnarr]]
  fnarr[0].incr();

// CHECK:      [[gsarr_0:%[0-9]+]] = OpAccessChain %_ptr_Workgroup_S %gsarr %int_0
// CHECK-NEXT: [[gs_arr_ld:%[0-9]+]] = OpLoad %S [[gsarr_0]]
// CHECK-NEXT:                        OpStore %temp_var_hlsl_inout_3 [[gs_arr_ld]]
// CHECK-NEXT:              {{%[0-9]+}} = OpFunctionCall %void %decr2 %temp_var_hlsl_inout_3
// CHECK-NEXT: [[gs_arr_wb:%[0-9]+]] = OpLoad %S %temp_var_hlsl_inout_3
// CHECK:                             OpStore {{%[0-9]+}} [[gs_arr_wb]]
  decr2(gsarr[0]);

// CHECK:      [[starr_0:%[0-9]+]] = OpAccessChain %_ptr_Private_S %starr %int_0
// CHECK-NEXT: [[st_arr_ld:%[0-9]+]] = OpLoad %S [[starr_0]]
// CHECK-NEXT:                        OpStore %temp_var_hlsl_inout_4 [[st_arr_ld]]
// CHECK-NEXT:              {{%[0-9]+}} = OpFunctionCall %void %decr2 %temp_var_hlsl_inout_4
// CHECK-NEXT: [[st_arr_wb:%[0-9]+]] = OpLoad %S %temp_var_hlsl_inout_4
// CHECK:                             OpStore {{%[0-9]+}} [[st_arr_wb]]
  decr2(starr[0]);

// CHECK:      [[fnarr_0:%[0-9]+]] = OpAccessChain %_ptr_Function_S %fnarr %int_0
// CHECK-NEXT: [[fn_arr_ld:%[0-9]+]] = OpLoad %S [[fnarr_0]]
// CHECK-NEXT:                        OpStore %temp_var_hlsl_inout_5 [[fn_arr_ld]]
// CHECK-NEXT:              {{%[0-9]+}} = OpFunctionCall %void %decr2 %temp_var_hlsl_inout_5
// CHECK-NEXT: [[fn_arr_wb:%[0-9]+]] = OpLoad %S %temp_var_hlsl_inout_5
// CHECK:                             OpStore {{%[0-9]+}} [[fn_arr_wb]]
  decr2(fnarr[0]);

// CHECK:       {{%[0-9]+}} = OpFunctionCall %void %int_decr %hlsl_out
// CHECK-NEXT: [[hl_ld:%[0-9]+]] = OpLoad %int %hlsl_out
// CHECK:      [[arr:%[0-9]+]] = OpAccessChain %_ptr_Function_int %arr %int_0
// CHECK:                       OpStore [[arr]] {{%[0-9]+}}
// CHECK-NEXT:                  OpStore [[arr]] [[hl_ld]]
  int_decr(++arr[0]);
}

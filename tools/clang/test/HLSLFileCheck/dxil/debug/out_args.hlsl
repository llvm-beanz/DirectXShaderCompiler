// RUN: %dxc -E main -T ps_6_0 %s -Zi -O0 | FileCheck %s

// CHECK-NOT: DW_OP_deref

// arg0 gets initialized in foo to {1,2,3}.
// CHECK: call void @llvm.dbg.value(metadata <3 x float> <float 1.000000e+00, float 2.000000e+00, float 3.000000e+00>, i64 0, metadata {{![0-9]+}}, metadata {{![0-9]+}}), !dbg {{![0-9]+}} ; var:"arg0" !DIExpression() func:"foo"

// output gets initialized in main to {1,2,3} on the return of foo.
// CHECK: call void @llvm.dbg.value(metadata <3 x float> <float 1.000000e+00, float 2.000000e+00, float 3.000000e+00>, i64 0, metadata {{![0-9]+}}, metadata {{![0-9]+}}), !dbg {{![0-9]+}} ; var:"output" !DIExpression() func:"main"

// arg1 gets initialized in main to {1,2,3} by copying from output before the
// call to bar.
// CHECK: call void @llvm.dbg.value(metadata <3 x float> <float 1.000000e+00, float 2.000000e+00, float 3.000000e+00>, i64 0, metadata {{![0-9]+}}, metadata {{![0-9]+}}), !dbg {{![0-9]+}} ; var:"arg1" !DIExpression() func:"bar"

// arg1 gets updated in bar to {2, 4, 6}.
// CHECK: call void @llvm.dbg.value(metadata float 2.000000e+00, i64 0, metadata {{![0-9]+}}, metadata {{![0-9]+}}), !dbg {{![0-9]+}} ; var:"arg1" !DIExpression(DW_OP_bit_piece, 0, 32) func:"bar"
// CHECK-NEXT: call void @llvm.dbg.value(metadata float 4.000000e+00, i64 0, metadata {{![0-9]+}}, metadata {{![0-9]+}}), !dbg {{![0-9]+}} ; var:"arg1" !DIExpression(DW_OP_bit_piece, 32, 32) func:"bar"
// CHECK-NEXT: call void @llvm.dbg.value(metadata float 6.000000e+00, i64 0, metadata {{![0-9]+}}, metadata {{![0-9]+}}), !dbg {{![0-9]+}} ; var:"arg1" !DIExpression(DW_OP_bit_piece, 64, 32) func:"bar"

// arg1 gets copied back to output on the return of bar, setting output to {2,
// 4, 6}.
// CHECK: call void @llvm.dbg.value(metadata float 2.000000e+00, i64 0, metadata {{![0-9]+}}, metadata {{![0-9]+}}), !dbg {{![0-9]+}} ; var:"output" !DIExpression(DW_OP_bit_piece, 0, 32) func:"main"
// CHECK-NEXT: call void @llvm.dbg.value(metadata float 4.000000e+00, i64 0, metadata {{![0-9]+}}, metadata {{![0-9]+}}), !dbg {{![0-9]+}} ; var:"output" !DIExpression(DW_OP_bit_piece, 32, 32) func:"main"
// CHECK-NEXT: call void @llvm.dbg.value(metadata float 6.000000e+00, i64 0, metadata {{![0-9]+}}, metadata {{![0-9]+}}), !dbg {{![0-9]+}} ; var:"output" !DIExpression(DW_OP_bit_piece, 64, 32) func:"main"

// CHECK-NOT: DW_OP_deref
// CHECK: !llvm.dbg.cu

void foo(out float3 arg0) {
  arg0 = float3(1,2,3); // @BREAK
  return;
}

void bar(inout float3 arg1) {
  arg1 += float3(1,2,3);
  return;
}

[RootSignature("")]
float3 main() : SV_Target {
  float3 output;
  foo(output);
  bar(output);
  return output;
}

// Exclude quoted source file (see readme)
// CHECK-LABEL: {{!"[^"]*\\0A[^"]*"}}


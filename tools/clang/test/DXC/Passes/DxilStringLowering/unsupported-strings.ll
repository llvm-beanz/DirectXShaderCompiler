; RUN: not opt %s -internalize -internalize-public-api-list=main,argument -globaldce -hlsl-dxilload -dxil-string-lowering -S -o /dev/null 2>&1 | FileCheck %s
; RUN: not opt %s -internalize -internalize-public-api-list=main,loaded -globaldce -hlsl-dxilload -dxil-string-lowering -S -o /dev/null 2>&1 | FileCheck %s
; RUN: not opt %s -internalize -internalize-public-api-list=main,selected -globaldce -hlsl-dxilload -dxil-string-lowering -S -o /dev/null 2>&1 | FileCheck %s
; RUN: not opt %s -internalize -internalize-public-api-list=main,called -globaldce -hlsl-dxilload -dxil-string-lowering -S -o /dev/null 2>&1 | FileCheck %s
; RUN: not opt %s -internalize -internalize-public-api-list=main,gep -globaldce -hlsl-dxilload -dxil-string-lowering -S -o /dev/null 2>&1 | FileCheck %s
; RUN: not opt %s -internalize -internalize-public-api-list=main,undefined -globaldce -hlsl-dxilload -dxil-string-lowering -S -o /dev/null 2>&1 | FileCheck %s

; CHECK: error: string-to-offset argument must resolve to constant strings through select or phi nodes
; CHECK-NOT: LLVM ERROR

target datalayout = "e-m:e-p:32:32-i1:32-i8:32-i16:32-i32:32-i64:64-f16:32-f32:32-f64:64-n8:16:32:64"
target triple = "dxil-ms-dx"

@text = private constant [6 x i8] c"first\00"
@pointer = external global i8*

declare i32 @llvm.string.to.offset(i8*)
declare i8* @unknown()

define void @main() {
  ret void
}

define i32 @argument(i8* %argument) {
entry:
  %a = call i32 @llvm.string.to.offset(i8* %argument)
  ret i32 %a
}

define i32 @loaded() {
entry:
  %loaded = load i8*, i8** @pointer
  %b = call i32 @llvm.string.to.offset(i8* %loaded)
  ret i32 %b
}

define i32 @selected(i1 %condition) {
entry:
  %loaded = load i8*, i8** @pointer
  %selected = select i1 %condition, i8* getelementptr ([6 x i8], [6 x i8]* @text, i32 0, i32 0), i8* %loaded
  %c = call i32 @llvm.string.to.offset(i8* %selected)
  ret i32 %c
}

define i32 @called() {
entry:
  %called = call i8* @unknown()
  %d = call i32 @llvm.string.to.offset(i8* %called)
  ret i32 %d
}

define i32 @gep(i32 %index) {
entry:
  %gep = getelementptr [6 x i8], [6 x i8]* @text, i32 0, i32 %index
  %e = call i32 @llvm.string.to.offset(i8* %gep)
  ret i32 %e
}

define i32 @undefined() {
entry:
  %f = call i32 @llvm.string.to.offset(i8* undef)
  ret i32 %f
}

!pauseresume = !{!0}
!dx.version = !{!1}
!dx.valver = !{!1}
!dx.shaderModel = !{!2}
!dx.entryPoints = !{!3}
!0 = !{!"hlsl-dxilemit", !"hlsl-dxilload"}
!1 = !{i32 1, i32 0}
!2 = !{!"cs", i32 6, i32 0}
!3 = !{void ()* @main, !"main", null, null, !4}
!4 = !{i32 4, !5}
!5 = !{i32 1, i32 1, i32 1}

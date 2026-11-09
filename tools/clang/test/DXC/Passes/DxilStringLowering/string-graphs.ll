; RUN: %dxopt %s -hlsl-passes-resume -dxil-string-lowering -verify -hlsl-dxilemit -S | FileCheck %s

target datalayout = "e-m:e-p:32:32-i1:32-i8:32-i16:32-i32:32-i64:64-f16:32-f32:32-f64:64-n8:16:32:64"
target triple = "dxil-ms-dx"

@first = private constant [6 x i8] c"first\00"
@second = private constant [7 x i8] c"second\00"

declare i32 @llvm.string.to.offset(i8*)
declare void @consume(i32)

define void @main() {
  ret void
}

; CHECK-LABEL: define i32 @shared_select(
; CHECK-NEXT: entry:
; CHECK-NEXT: %text.offset = select i1 %condition, i32 [[FIRST:[0-9]+]], i32 [[SECOND:[0-9]+]]
; CHECK-NEXT: %sum = add i32 %text.offset, %text.offset
; CHECK-NEXT: ret i32 %sum
define i32 @shared_select(i1 %condition) {
entry:
  %text = select i1 %condition, i8* getelementptr ([6 x i8], [6 x i8]* @first, i32 0, i32 0), i8* getelementptr ([7 x i8], [7 x i8]* @second, i32 0, i32 0)
  %a = call i32 @llvm.string.to.offset(i8* %text)
  %b = call i32 @llvm.string.to.offset(i8* %text)
  %sum = add i32 %a, %b
  ret i32 %sum
}

; CHECK-LABEL: define i32 @branch_phi(
; CHECK: merge:
; CHECK-NEXT: %text.offset = phi i32 [ [[FIRST]], %first.block ], [ [[SECOND]], %second.block ]
; CHECK-NEXT: ret i32 %text.offset
define i32 @branch_phi(i1 %condition) {
entry:
  br i1 %condition, label %first.block, label %second.block
first.block:
  br label %merge
second.block:
  br label %merge
merge:
  %text = phi i8* [ getelementptr ([6 x i8], [6 x i8]* @first, i32 0, i32 0), %first.block ], [ getelementptr ([7 x i8], [7 x i8]* @second, i32 0, i32 0), %second.block ]
  %offset = call i32 @llvm.string.to.offset(i8* %text)
  ret i32 %offset
}

; CHECK-LABEL: define i32 @loop_cycle(
; CHECK: loop:
; CHECK-NEXT: %text.offset = phi i32 [ [[FIRST]], %entry ], [ %next.offset, %loop ]
; CHECK-NEXT: %other.offset = phi i32 [ [[SECOND]], %entry ], [ %text.offset, %loop ]
; CHECK-NEXT: %next.offset = select i1 %condition, i32 %other.offset, i32 %text.offset
; CHECK-NEXT: call void @consume(i32 %next.offset)
; CHECK-NEXT: br i1 %repeat, label %loop, label %exit
; CHECK: exit:
; CHECK-NEXT: ret i32 %text.offset
define i32 @loop_cycle(i1 %condition, i1 %repeat) {
entry:
  br label %loop
loop:
  %text = phi i8* [ getelementptr ([6 x i8], [6 x i8]* @first, i32 0, i32 0), %entry ], [ %next, %loop ]
  %other = phi i8* [ getelementptr ([7 x i8], [7 x i8]* @second, i32 0, i32 0), %entry ], [ %text, %loop ]
  %next = select i1 %condition, i8* %other, i8* %text
  %offset = call i32 @llvm.string.to.offset(i8* %next)
  call void @consume(i32 %offset)
  br i1 %repeat, label %loop, label %exit
exit:
  %result = call i32 @llvm.string.to.offset(i8* %text)
  ret i32 %result
}

; CHECK-LABEL: define i8* @preserve_external_use(
; CHECK: %text.offset = phi i32 [ [[FIRST]], %entry ], [ %next.offset, %loop ]
; CHECK-NEXT: %text = phi i8*
; CHECK-NEXT: %next.offset = select i1 %condition, i32 [[SECOND]], i32 %text.offset
; CHECK-NEXT: %next = select i1 %condition, i8*
; CHECK-NEXT: call void @consume(i32 %next.offset)
; CHECK: ret i8* %text
define i8* @preserve_external_use(i1 %condition, i1 %repeat) {
entry:
  br label %loop
loop:
  %text = phi i8* [ getelementptr ([6 x i8], [6 x i8]* @first, i32 0, i32 0), %entry ], [ %next, %loop ]
  %next = select i1 %condition, i8* getelementptr ([7 x i8], [7 x i8]* @second, i32 0, i32 0), i8* %text
  %offset = call i32 @llvm.string.to.offset(i8* %next)
  call void @consume(i32 %offset)
  br i1 %repeat, label %loop, label %exit
exit:
  ret i8* %text
}

; CHECK-NOT: @llvm.string.to.offset
; CHECK: !dx.strtab =

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

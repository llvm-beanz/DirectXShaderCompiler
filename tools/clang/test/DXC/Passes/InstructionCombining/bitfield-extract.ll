; RUN: %dxopt %s -hlsl-passes-resume -instcombine -S | FileCheck %s

;
; Buffer Definitions:
;
; Resource bind info for result0
; {
;
;   uint $Element;                                    ; Offset:    0 Size:     4
;
; }
;
; Resource bind info for result1
; {
;
;   int $Element;                                     ; Offset:    0 Size:     4
;
; }
;
;
; Resource Bindings:
;
; Name                                 Type  Format         Dim      ID      HLSL Bind  Count
; ------------------------------ ---------- ------- ----------- ------- -------------- ------
; result0                               UAV  struct         r/w      U0u4294967295,space4294967295     1
; result1                               UAV  struct         r/w      U1u4294967295,space4294967295     1
;
target datalayout = "e-m:e-p:32:32-i1:32-i8:32-i16:32-i32:32-i64:64-f16:32-f32:32-f64:64-n8:16:32:64"
target triple = "dxil-ms-dx"

%"class.RWStructuredBuffer<unsigned int>" = type { i32 }
%"class.RWStructuredBuffer<int>" = type { i32 }
%dx.types.Handle = type { i8* }
%dx.types.ResourceProperties = type { i32, i32 }
%dx.types.ResRet.i32 = type { i32, i32, i32, i32, i32 }

@"\01?result0@@3V?$RWStructuredBuffer@I@@A" = external global %"class.RWStructuredBuffer<unsigned int>", align 4
@"\01?result1@@3V?$RWStructuredBuffer@H@@A" = external global %"class.RWStructuredBuffer<int>", align 4
@llvm.used = appending global [2 x i8*] [i8* bitcast (%"class.RWStructuredBuffer<unsigned int>"* @"\01?result0@@3V?$RWStructuredBuffer@I@@A" to i8*), i8* bitcast (%"class.RWStructuredBuffer<int>"* @"\01?result1@@3V?$RWStructuredBuffer@H@@A" to i8*)], section "llvm.metadata"

; CHECK-DAG: {{.*}} = call i32 @dx.op.tertiary.i32(i32 51, i32 [[Value:%.*]], i32 [[Width:%.*]], i32 [[Offset:%.*]])
; CHECK-DAG: [[Value]] = extractvalue %dx.types.ResRet.i32 [[ValueLoad:%.*]], 0
; CHECK-DAG: [[ValueLoad]] = call %dx.types.ResRet.i32 @dx.op.rawBufferLoad.i32(i32 139, %dx.types.Handle {{.*}}, i32 1, i32 0, i8 1, i32 4)

; CHECK-DAG: [[Width]] = extractvalue %dx.types.ResRet.i32 [[WidthLoad:%.*]], 0
; CHECK-DAG: [[Offset]] = extractvalue %dx.types.ResRet.i32 [[OffsetLoad:%.*]], 0

; CHECK-DAG: [[WidthLoad]] = call %dx.types.ResRet.i32 @dx.op.rawBufferLoad.i32(i32 139, %dx.types.Handle [[HandleTwo:%.*]], i32 1, i32 0, i8 1, i32 4)
; CHECK-DAG: [[OffsetLoad]] = call %dx.types.ResRet.i32 @dx.op.rawBufferLoad.i32(i32 139, %dx.types.Handle [[HandleTwo:%.*]], i32 0, i32 0, i8 1, i32 4)

; Function Attrs: nounwind
define void @main() #0 {
  %1 = load %"class.RWStructuredBuffer<int>", %"class.RWStructuredBuffer<int>"* @"\01?result1@@3V?$RWStructuredBuffer@H@@A", !dbg !23 ; line:15 col:15
  %2 = load %"class.RWStructuredBuffer<unsigned int>", %"class.RWStructuredBuffer<unsigned int>"* @"\01?result0@@3V?$RWStructuredBuffer@I@@A", !dbg !27 ; line:16 col:5
  %3 = call %dx.types.Handle @"dx.op.createHandleForLib.class.RWStructuredBuffer<unsigned int>"(i32 160, %"class.RWStructuredBuffer<unsigned int>" %2), !dbg !28 ; line:13 col:16  ; CreateHandleForLib(Resource)
  %4 = call %dx.types.Handle @dx.op.annotateHandle(i32 216, %dx.types.Handle %3, %dx.types.ResourceProperties { i32 4108, i32 4 }), !dbg !28 ; line:13 col:16  ; AnnotateHandle(res,props)  resource: RWStructuredBuffer<stride=4>
  %RawBufferLoad2 = call %dx.types.ResRet.i32 @dx.op.rawBufferLoad.i32(i32 139, %dx.types.Handle %4, i32 1, i32 0, i8 1, i32 4), !dbg !28 ; line:13 col:16  ; RawBufferLoad(srv,index,elementOffset,mask,alignment)
  %5 = extractvalue %dx.types.ResRet.i32 %RawBufferLoad2, 0, !dbg !28 ; line:13 col:16
  %6 = call %dx.types.Handle @"dx.op.createHandleForLib.class.RWStructuredBuffer<int>"(i32 160, %"class.RWStructuredBuffer<int>" %1), !dbg !29 ; line:14 col:15  ; CreateHandleForLib(Resource)
  %7 = call %dx.types.Handle @dx.op.annotateHandle(i32 216, %dx.types.Handle %6, %dx.types.ResourceProperties { i32 4108, i32 4 }), !dbg !29 ; line:14 col:15  ; AnnotateHandle(res,props)  resource: RWStructuredBuffer<stride=4>
  %RawBufferLoad1 = call %dx.types.ResRet.i32 @dx.op.rawBufferLoad.i32(i32 139, %dx.types.Handle %7, i32 0, i32 0, i8 1, i32 4), !dbg !29 ; line:14 col:15  ; RawBufferLoad(srv,index,elementOffset,mask,alignment)
  %8 = extractvalue %dx.types.ResRet.i32 %RawBufferLoad1, 0, !dbg !29 ; line:14 col:15
  %9 = call %dx.types.Handle @"dx.op.createHandleForLib.class.RWStructuredBuffer<int>"(i32 160, %"class.RWStructuredBuffer<int>" %1), !dbg !23 ; line:15 col:15  ; CreateHandleForLib(Resource)
  %10 = call %dx.types.Handle @dx.op.annotateHandle(i32 216, %dx.types.Handle %9, %dx.types.ResourceProperties { i32 4108, i32 4 }), !dbg !23 ; line:15 col:15  ; AnnotateHandle(res,props)  resource: RWStructuredBuffer<stride=4>
  %RawBufferLoad = call %dx.types.ResRet.i32 @dx.op.rawBufferLoad.i32(i32 139, %dx.types.Handle %10, i32 1, i32 0, i8 1, i32 4), !dbg !23 ; line:15 col:15  ; RawBufferLoad(srv,index,elementOffset,mask,alignment)
  %11 = extractvalue %dx.types.ResRet.i32 %RawBufferLoad, 0, !dbg !23 ; line:15 col:15
  %12 = and i32 %8, 31, !dbg !30 ; line:6 col:19
  %13 = lshr i32 %5, %12, !dbg !30 ; line:6 col:19
  %14 = and i32 %11, 31, !dbg !33 ; line:6 col:37
  %15 = shl i32 1, %14, !dbg !33 ; line:6 col:37
  %16 = sub i32 %15, 1, !dbg !34 ; line:6 col:46
  %17 = and i32 %13, %16, !dbg !35 ; line:6 col:30
  %18 = call %dx.types.Handle @"dx.op.createHandleForLib.class.RWStructuredBuffer<unsigned int>"(i32 160, %"class.RWStructuredBuffer<unsigned int>" %2), !dbg !27 ; line:16 col:5  ; CreateHandleForLib(Resource)
  %19 = call %dx.types.Handle @dx.op.annotateHandle(i32 216, %dx.types.Handle %18, %dx.types.ResourceProperties { i32 4108, i32 4 }), !dbg !27 ; line:16 col:5  ; AnnotateHandle(res,props)  resource: RWStructuredBuffer<stride=4>
  call void @dx.op.rawBufferStore.i32(i32 140, %dx.types.Handle %19, i32 0, i32 0, i32 %17, i32 undef, i32 undef, i32 undef, i8 1, i32 4), !dbg !36 ; line:16 col:16  ; RawBufferStore(uav,index,elementOffset,value0,value1,value2,value3,mask,alignment)
  ret void, !dbg !37 ; line:17 col:1
}

; Function Attrs: nounwind readnone
declare %dx.types.Handle @"dx.hl.createhandle..%dx.types.Handle (i32, %\22class.RWStructuredBuffer<unsigned int>\22)"(i32, %"class.RWStructuredBuffer<unsigned int>") #1

; Function Attrs: nounwind readnone
declare %dx.types.Handle @"dx.hl.annotatehandle..%dx.types.Handle (i32, %dx.types.Handle, %dx.types.ResourceProperties, %\22class.RWStructuredBuffer<unsigned int>\22)"(i32, %dx.types.Handle, %dx.types.ResourceProperties, %"class.RWStructuredBuffer<unsigned int>") #1

; Function Attrs: nounwind readnone
declare %dx.types.Handle @"dx.hl.createhandle..%dx.types.Handle (i32, %\22class.RWStructuredBuffer<int>\22)"(i32, %"class.RWStructuredBuffer<int>") #1

; Function Attrs: nounwind readnone
declare %dx.types.Handle @"dx.hl.annotatehandle..%dx.types.Handle (i32, %dx.types.Handle, %dx.types.ResourceProperties, %\22class.RWStructuredBuffer<int>\22)"(i32, %dx.types.Handle, %dx.types.ResourceProperties, %"class.RWStructuredBuffer<int>") #1

; Function Attrs: nounwind
declare void @dx.op.rawBufferStore.i32(i32, %dx.types.Handle, i32, i32, i32, i32, i32, i32, i8, i32) #0

; Function Attrs: nounwind readonly
declare %dx.types.ResRet.i32 @dx.op.rawBufferLoad.i32(i32, %dx.types.Handle, i32, i32, i8, i32) #2

; Function Attrs: nounwind readonly
declare %dx.types.Handle @"dx.op.createHandleForLib.class.RWStructuredBuffer<unsigned int>"(i32, %"class.RWStructuredBuffer<unsigned int>") #2

; Function Attrs: nounwind readnone
declare %dx.types.Handle @dx.op.annotateHandle(i32, %dx.types.Handle, %dx.types.ResourceProperties) #1

; Function Attrs: nounwind readonly
declare %dx.types.Handle @"dx.op.createHandleForLib.class.RWStructuredBuffer<int>"(i32, %"class.RWStructuredBuffer<int>") #2

attributes #0 = { nounwind }
attributes #1 = { nounwind readnone }
attributes #2 = { nounwind readonly }

!llvm.module.flags = !{!0}
!pauseresume = !{!1}
!llvm.ident = !{!2}
!dx.version = !{!3}
!dx.valver = !{!4}
!dx.shaderModel = !{!5}
!dx.resources = !{!6}
!dx.typeAnnotations = !{!11, !16}
!dx.entryPoints = !{!20}

!0 = !{i32 2, !"Debug Info Version", i32 3}
!1 = !{!"hlsl-dxilemit", !"hlsl-dxilload"}
!2 = !{!"dxc(private) 1.8.0.14717 (main, b766b432678-dirty)"}
!3 = !{i32 1, i32 0}
!4 = !{i32 1, i32 8}
!5 = !{!"cs", i32 6, i32 0}
!6 = !{null, !7, null, null}
!7 = !{!8, !10}
!8 = !{i32 0, %"class.RWStructuredBuffer<unsigned int>"* @"\01?result0@@3V?$RWStructuredBuffer@I@@A", !"result0", i32 -1, i32 -1, i32 1, i32 12, i1 false, i1 false, i1 false, !9}
!9 = !{i32 1, i32 4}
!10 = !{i32 1, %"class.RWStructuredBuffer<int>"* @"\01?result1@@3V?$RWStructuredBuffer@H@@A", !"result1", i32 -1, i32 -1, i32 1, i32 12, i1 false, i1 false, i1 false, !9}
!11 = !{i32 0, %"class.RWStructuredBuffer<unsigned int>" undef, !12, %"class.RWStructuredBuffer<int>" undef, !14}
!12 = !{i32 4, !13}
!13 = !{i32 6, !"h", i32 3, i32 0, i32 7, i32 5}
!14 = !{i32 4, !15}
!15 = !{i32 6, !"h", i32 3, i32 0, i32 7, i32 4}
!16 = !{i32 1, void ()* @main, !17}
!17 = !{!18}
!18 = !{i32 1, !19, !19}
!19 = !{}
!20 = !{void ()* @main, !"main", null, !6, !21}
!21 = !{i32 4, !22}
!22 = !{i32 1, i32 1, i32 1}
!23 = !DILocation(line: 15, column: 15, scope: !24)
!24 = !DISubprogram(name: "main", scope: !25, file: !25, line: 11, type: !26, isLocal: false, isDefinition: true, scopeLine: 12, flags: DIFlagPrototyped, isOptimized: false, function: void ()* @main)
!25 = !DIFile(filename: "./bfi.hlsl", directory: "")
!26 = !DISubroutineType(types: !19)
!27 = !DILocation(line: 16, column: 5, scope: !24)
!28 = !DILocation(line: 13, column: 16, scope: !24)
!29 = !DILocation(line: 14, column: 15, scope: !24)
!30 = !DILocation(line: 6, column: 19, scope: !31, inlinedAt: !32)
!31 = !DISubprogram(name: "bitfieldExtract", scope: !25, file: !25, line: 4, type: !26, isLocal: false, isDefinition: true, scopeLine: 5, flags: DIFlagPrototyped, isOptimized: false)
!32 = distinct !DILocation(line: 16, column: 18, scope: !24)
!33 = !DILocation(line: 6, column: 37, scope: !31, inlinedAt: !32)
!34 = !DILocation(line: 6, column: 46, scope: !31, inlinedAt: !32)
!35 = !DILocation(line: 6, column: 30, scope: !31, inlinedAt: !32)
!36 = !DILocation(line: 16, column: 16, scope: !24)
!37 = !DILocation(line: 17, column: 1, scope: !24)

// RUN: %dxc -T lib_6_x -fcgl %s | FileCheck %s --check-prefix=FCGL
// RUN: %dxc -T lib_6_x -ast-dump %s | FileCheck %s --check-prefix=AST

// Test that inout parameters are represented as reference types in the AST
// and that lvalue operations (+=) work correctly on them.

export void fn(inout float3 a, float3 b) {
  a += b;
}

// AST: FunctionDecl {{.*}} fn 'void (float3 &__restrict, float3)'
// AST: ParmVarDecl {{.*}} a 'float3 &__restrict'
// AST-NEXT: HLSLInOutAttr
// AST: ParmVarDecl {{.*}} b 'float3{{.*}}'
// No HLSLInOutAttr on b - it's a plain input
// AST-NOT: HLSLInOutAttr
// AST: CompoundAssignOperator {{.*}} '+='
// AST: DeclRefExpr {{.*}} 'a' 'float3{{.*}}'

// FCGL: define void @{{.*fn.*}}(<3 x float>* noalias dereferenceable(12) %a, <3 x float> %b)
// FCGL: %[[BVAL:[0-9]+]] = load <3 x float>, <3 x float>*
// FCGL: %[[AVAL:[0-9]+]] = load <3 x float>, <3 x float>* %a
// FCGL: %[[SUM:[0-9]+]] = fadd <3 x float> %[[AVAL]], %[[BVAL]]
// FCGL: store <3 x float> %[[SUM]], <3 x float>* %a

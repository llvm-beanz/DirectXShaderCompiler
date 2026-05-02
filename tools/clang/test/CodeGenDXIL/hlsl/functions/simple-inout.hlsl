// RUN: %dxc -T vs_6_0 -fcgl %s | FileCheck %s --check-prefix=FCGL
// RUN: %dxc -T vs_6_0 -ast-dump %s | FileCheck %s --check-prefix=AST

// Test basic inout parameter with implicit type conversion.
// When val is passed for both the float and int inout parameters, the compiler
// must create temporaries and perform copy-in/copy-out with type conversion.

void fn(inout float x, inout int y) {
  y = 2;
  x = 1;
}

float main(float val: A) : B {
  fn(val, val);
  return val;
}

// AST: FunctionDecl {{.*}} fn 'void (float &__restrict, int &__restrict)'
// AST: ParmVarDecl {{.*}} x 'float &__restrict'
// AST-NEXT: HLSLInOutAttr
// AST: ParmVarDecl {{.*}} y 'int &__restrict'
// AST-NEXT: HLSLInOutAttr

// AST: HLSLOutArgExpr {{.*}} inout
// AST: OpaqueValueExpr {{.*}} 'float' lvalue
// AST: DeclRefExpr {{.*}} 'val' 'float'
// AST: OpaqueValueExpr {{.*}} 'float' lvalue
// AST: ImplicitCastExpr {{.*}} 'float' <LValueToRValue>
// AST: BinaryOperator {{.*}} 'float' '='

// AST: HLSLOutArgExpr {{.*}} inout
// AST: OpaqueValueExpr {{.*}} 'float' lvalue
// AST: DeclRefExpr {{.*}} 'val' 'float'
// AST: OpaqueValueExpr {{.*}} 'int' lvalue
// AST: ImplicitCastExpr {{.*}} 'int' <FloatingToIntegral>
// AST: BinaryOperator {{.*}} 'float' '='
// AST: ImplicitCastExpr {{.*}} 'float' <IntegralToFloating>

// FCGL: define float @main(float %val)
// There are three allocas: val temp (dx.temp), int temp, float temp
// FCGL: alloca float{{.*}}dx.temp
// FCGL: %[[TMP_INT:[0-9]+]] = alloca i32
// FCGL: %[[TMP_FLOAT:[0-9]+]] = alloca float
// Copy float val into the int temporary with conversion (fptosi)
// FCGL: %[[V:[0-9]+]] = load float, float*
// FCGL: %[[I:[0-9]+]] = fptosi float %[[V]] to i32
// FCGL: store i32 %[[I]], i32* %[[TMP_INT]]
// Copy float val into the float temporary
// FCGL: %[[V2:[0-9]+]] = load float, float*
// FCGL: store float %[[V2]], float* %[[TMP_FLOAT]]
// FCGL: call void @{{.*fn.*}}(float* dereferenceable(4) %[[TMP_FLOAT]], i32* dereferenceable(4) %[[TMP_INT]])
// Copy float temporary back with no conversion needed
// FCGL: %[[R1:[0-9]+]] = load float, float* %[[TMP_FLOAT]]
// FCGL: store float %[[R1]], float*
// Copy int temporary back to float val with conversion (sitofp)
// FCGL: %[[R2:[0-9]+]] = load i32, i32* %[[TMP_INT]]
// FCGL: %[[R3:[0-9]+]] = sitofp i32 %[[R2]] to float
// FCGL: store float %[[R3]], float*

// RUN: %dxc -Tlib_6_3 -HV 202x -ast-dump %s | FileCheck %s

// Verifies the AST shape produced when an 'explicit' user-defined conversion
// operator is invoked through an explicit cast in HLSL 202x.

struct A {
    float X, Y;
};

struct B {
    int2 V;

    explicit operator A() {
        A a;
        a.X = V.x;
        a.Y = V.y;
        return a;
    }
};

float takeA(A a) { return a.X + a.Y; }

void fn(B b1) {
    // C-style cast should invoke the explicit user-defined operator.
    // CHECK: VarDecl {{.*}} a1 'A' cinit
    // CHECK: ImplicitCastExpr {{.*}} 'A' <UserDefinedConversion>
    // CHECK-NEXT: CXXMemberCallExpr {{.*}} 'A'
    // CHECK-NEXT: MemberExpr {{.*}} .operator A
    A a1 = (A)b1;

    // Explicit cast as a call argument is allowed and invokes the operator.
    // CHECK: CallExpr {{.*}} 'float'
    // CHECK: ImplicitCastExpr {{.*}} 'A' <UserDefinedConversion>
    // CHECK-NEXT: CXXMemberCallExpr {{.*}} 'A'
    // CHECK-NEXT: MemberExpr {{.*}} .operator A
    float r = takeA((A)b1);
}

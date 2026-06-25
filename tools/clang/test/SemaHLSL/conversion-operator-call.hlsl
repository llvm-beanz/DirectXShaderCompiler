// RUN: %dxc -Tlib_6_3 -HV 202x -ast-dump %s | FileCheck %s

// Verifies that HLSL 202x allows user-defined conversion operators to be
// declared and invoked. Implicit conversions, explicit C-style casts, and
// call-argument copy-initialization all invoke the operator.

struct A {
    float X, Y;
};

struct B {
    int2 V;

    operator A() {
        A a;
        a.X = V.x;
        a.Y = V.y;
        return a;
    }
};

float takeA(A a) { return a.X + a.Y; }

void fn(float2 f, B b1) {
    // Implicit conversion from B to A should invoke B::operator A().
    // CHECK: VarDecl {{.*}} a3 'A' cinit
    // CHECK-NEXT: CXXConstructExpr {{.*}} 'A' 'void (const A &)
    // CHECK-NEXT: ImplicitCastExpr {{.*}} 'A' <UserDefinedConversion>
    // CHECK-NEXT: CXXMemberCallExpr {{.*}} 'A'
    // CHECK-NEXT: MemberExpr {{.*}} .operator A
    A a3 = b1;

    // Explicit cast should invoke the user-defined operator.
    // CHECK: VarDecl {{.*}} a4 'A' cinit
    // CHECK: ImplicitCastExpr {{.*}} 'A' <UserDefinedConversion>
    // CHECK-NEXT: CXXMemberCallExpr {{.*}} 'A'
    // CHECK-NEXT: MemberExpr {{.*}} .operator A
    A a4 = (A)b1;

    // Call-argument copy initialization should also invoke the operator.
    // CHECK: CallExpr {{.*}} 'float'
    // CHECK: ImplicitCastExpr {{.*}} 'A' <UserDefinedConversion>
    // CHECK-NEXT: CXXMemberCallExpr {{.*}} 'A'
    // CHECK-NEXT: MemberExpr {{.*}} .operator A
    float r = takeA(b1);
}

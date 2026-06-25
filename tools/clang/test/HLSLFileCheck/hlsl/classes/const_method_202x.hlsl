// RUN: %dxc -T ps_6_0 -E main -HV 202x -ast-dump %s | FileCheck %s

// Verify the parser accepts `const` instance methods in HLSL 202x and that
// overload resolution selects the const overload for const objects and the
// non-const overload for non-const objects.

struct S {
  int x;
  int get() { return 100; }
  int get() const { return 200; }
};

// Two distinct overloads: one const-qualified, one not.
// CHECK: CXXMethodDecl [[NC:0x[0-9a-f]+]] {{.*}} used get 'int ()'
// CHECK: CXXMethodDecl [[C:0x[0-9a-f]+]] {{.*}} used get 'int () const'

cbuffer CB {
  S cs; // cs is const because it lives in a cbuffer.
};

float4 main() : SV_Target {
  S s = {1};
  int a = s.get();   // expect non-const overload
  int b = cs.get();  // expect const overload
  return float4(a, b, 0, 0);
}

// CHECK: MemberExpr {{.*}} .get [[NC]]
// CHECK-NEXT: DeclRefExpr {{.*}} 'S' lvalue Var {{0x[0-9a-f]+}} 's' 'S'
// CHECK: MemberExpr {{.*}} .get [[C]]
// CHECK-NEXT: DeclRefExpr {{.*}} 'const S' lvalue Var {{0x[0-9a-f]+}} 'cs' 'const S'

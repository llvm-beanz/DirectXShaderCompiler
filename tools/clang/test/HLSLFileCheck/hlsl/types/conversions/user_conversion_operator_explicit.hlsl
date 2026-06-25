// RUN: %dxc -E main -T vs_6_2 -HV 202x %s | FileCheck %s

// Verifies that an 'explicit' user-defined conversion operator is invoked for
// explicit C-style and functional-style casts. Implicit conversions are
// excluded by Sema and are covered by conversion-operator-explicit.hlsl.

// CHECK: define void @main()
// CHECK: ret void

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

float consume(A a) { return a.X + a.Y; }

float main(int2 input : INPUT) : OUT {
    B b;
    b.V = input;

    A a1 = (A)b;        // C-style cast.
    float r = consume((A)b);

    return a1.X + a1.Y + r;
}

// RUN: %dxc -E main -T vs_6_2 -HV 202x %s | FileCheck %s

// Verifies that user-defined conversion operators are invoked for
// initializations and call-argument copy-initialization with a single
// source value of class type. User-defined conversion operators are only
// supported in HLSL 202x and later.

// CHECK: define void @main()
// The operator should be invoked - check that the conversion produces
// the result computed inside B::operator A(): a.X = V.x, a.Y = V.y.
// CHECK: ret void

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

float consume(A a) { return a.X + a.Y; }

float main(int2 input : INPUT) : OUT {
    B b;
    b.V = input;

    // Implicit conversion: should invoke B::operator A().
    A a1 = b;

    // Call-argument copy initialization: should invoke B::operator A().
    float r = consume(b);

    return a1.X + a1.Y + r;
}

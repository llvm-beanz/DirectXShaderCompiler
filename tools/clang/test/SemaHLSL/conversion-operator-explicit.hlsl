// RUN: %dxc -Tlib_6_3 -verify -HV 202x %s

// Verifies the semantics of 'explicit' user-defined conversion operators in
// HLSL 202x: an explicit operator may not participate in implicit conversions
// (copy initialization or call-argument passing), but is invoked for explicit
// C-style and functional-style casts.

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

void implicit_uses(B b) {
    // Copy initialization may not invoke an explicit operator.
    // expected-error@+1 {{cannot initialize a variable of type 'A' with an lvalue of type 'B'}}
    A a1 = b;

    // Call-argument copy initialization may not invoke an explicit operator.
    // expected-error@+2 {{no matching function for call to 'takeA'}}
    // expected-note@23 {{candidate function not viable}}
    float r = takeA(b);
}

void explicit_uses(B b) {
    // C-style cast may invoke an explicit operator.
    A a2 = (A)b;

    // Explicit cast as a call argument is allowed.
    float r1 = takeA((A)b);
}

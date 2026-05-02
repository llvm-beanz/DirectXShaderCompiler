// RUN: %dxc -T vs_6_0 %s -verify

// Test that casting an array type to a different array type is rejected
// when trying to pass an element as an inout parameter.
// Casting int[1] to float[1] decays to a pointer conversion which is not valid.

typedef int ai32[1];
typedef float af32[1];
void inc(inout float x) { x *= -1; }
int main() : OUT
{
    ai32 x = { 42 };
    inc(((af32)x)[0]); // expected-error{{cannot convert from 'int *' to 'af32'}}
    return x[0];
}

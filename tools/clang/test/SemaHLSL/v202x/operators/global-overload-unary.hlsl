// RUN: %dxc -T lib_6_6 -HV 202x -verify %s

// HLSL 202x allows non-member (global) unary operator overloads for class
// and enumeration types. The unary operators +, -, !, and ~ that are part
// of the HLSL allow-list for overloading must be accepted as global
// declarations and be selected when applied to values of those types.

// expected-no-diagnostics

struct S {
  int v;
};

S operator-(S a) { S r; r.v = -a.v; return r; }
S operator+(S a) { return a; }
bool operator!(S a) { return a.v == 0; }
S operator~(S a) { S r; r.v = ~a.v; return r; }

enum E { A = 1, B = 2, C = 4 };

int operator-(E a) { return -(int)a; }
int operator+(E a) { return (int)a; }
bool operator!(E a) { return (int)a == 0; }
int operator~(E a) { return ~(int)a; }

export int test_unary_ops() {
  S s = {3};
  S neg = -s;
  S pos = +s;
  bool zero = !s;
  S comp = ~s;

  int e_neg = -A;
  int e_pos = +B;
  bool e_zero = !C;
  int e_comp = ~A;

  return neg.v + pos.v + (zero ? 1 : 0) + comp.v +
         e_neg + e_pos + (e_zero ? 1 : 0) + e_comp;
}

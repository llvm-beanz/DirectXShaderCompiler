// RUN: %dxc -T ps_6_0 -E main -HV 202x -verify %s

// Verify that const-correctness is enforced for HLSL 202x: a non-const
// instance method cannot be called on a const object, regardless of whether
// the const-ness comes from a cbuffer member, a ConstantBuffer<T>, the
// implicit global cbuffer, or an explicit `const` local.

struct S {
  int x;
  int get() const { return x; }
  int getNC() { return x; } // expected-note 4 {{'getNC' declared here}}
};

cbuffer CB {
  S cs;
};

ConstantBuffer<S> cb;

S g; // implicit global cbuffer member - implicitly const.

float4 main() : SV_Target {
  // OK: const method on each kind of const object.
  int a = cs.get();
  int b = cb.get();
  int c = g.get();
  const S ls = {1};
  int d = ls.get();

  // Error: non-const method on const object.
  int e = cs.getNC(); // expected-error {{member function 'getNC' not viable: 'this' argument has type 'const S', but function is not marked const}}
  int f = cb.getNC(); // expected-error {{member function 'getNC' not viable: 'this' argument has type 'const S', but function is not marked const}}
  int h = g.getNC();  // expected-error {{member function 'getNC' not viable: 'this' argument has type 'const S', but function is not marked const}}
  int i = ls.getNC(); // expected-error {{member function 'getNC' not viable: 'this' argument has type 'const S', but function is not marked const}}

  return float4(a, b, c, d) + float4(e, f, h, i);
}

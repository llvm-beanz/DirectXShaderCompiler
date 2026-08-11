// RUN: %dxc -T lib_6_3 -HV 202x -verify %s

// Negative-case coverage for HLSL 202x's variadic-template support.
//
// The variadic-templates feature enables C++-like template/function
// parameter packs and pack expansions, but it does not implicitly enable
// every piece of C++ variadic-template machinery. Some constructs remain
// deliberately unsupported because they depend on other C++ features HLSL
// has never supported (e.g. multiple/variadic inheritance), and ordinary
// C++ template rules about pack placement and overload resolution still
// apply. This file documents and locks down that behavior so future
// changes cannot silently regress it -- these are exactly the kinds of
// cases the previous, C++-test-suite-only "spot checks" of this feature
// did not cover.

// Base-class parameter packs remain unsupported: HLSL does not support
// multiple or variadic base classes at all (a struct may have at most one,
// fixed base type), so pack expansion in a base-specifier list is out of
// scope for the variadic-templates feature and must continue to be
// rejected, even in HLSL 202x.
struct Base1 {
  float x;
};
struct Base2 {
  float y;
};

template <typename... Bases>
struct Derived : Bases... {
  // expected-error@-1{{base type ellipsis is unsupported in HLSL}}
  // expected-error@-2{{multiple concrete base types specified}}
  float z;
};

void UseDerived() {
  Derived<Base1, Base2> d;
  // expected-note@-1{{in instantiation of template class 'Derived<Base1, Base2>' requested here}}
  d.z = 0;
}

// A template parameter pack must be the last template parameter in a class
// template's parameter list (ordinary C++ [temp.param] rule; not relaxed
// by HLSL 202x).
template <typename... Ts, typename U>
// expected-error@-1{{template parameter pack must be the last template parameter}}
struct PackNotLast {};

// sizeof...() only applies to the name of an actual parameter pack.
uint NotAPack() {
  return sizeof...(NotAPack);
  // expected-error@-1{{'NotAPack' does not refer to the name of a parameter pack}}
}

// C-style (non-template) variadic functions are a distinct, deliberate
// HLSL restriction unrelated to variadic templates, and remain rejected
// in HLSL 202x.
void CStyleVarArgs(int a, ...);
// expected-error@-1{{variadic arguments is unsupported in HLSL}}

// Calling a variadic template/member-template combination with a pack
// argument count that doesn't match the (already-instantiated) parameter
// list is an ordinary overload-resolution failure, not something HLSL
// 202x needs to specially diagnose.
template <typename... Ts>
struct Zipper {
  template <typename... Us>
  static uint Count(Ts... ts, Us... us) {
    // expected-note@-1{{candidate function not viable: requires 3 arguments, but 2 were provided}}
    return sizeof...(Ts) + sizeof...(Us);
  }
};

uint TestMismatchedPackArgs() {
  return Zipper<int, float>::Count<double>(1, 2.0);
  // expected-error@-1{{no matching function for call to 'Count'}}
}

// RUN: %dxc -T lib_6_3 -HV 202x -verify %s
// RUN: %dxc -T lib_6_3 -HV 202x -ast-dump %s 2>&1 | FileCheck %s

// HLSL 202x enables C++-like variadic templates: template parameter packs,
// function parameter packs, pack expansions, and sizeof...().

// expected-no-diagnostics

// Template type parameter pack.
// CHECK: FunctionTemplateDecl {{.*}} Sum
// CHECK: TemplateTypeParmDecl {{.*}} typename ... Rest
template <typename T, typename... Rest>
T Sum(T First, Rest... Others) {
  return First;
}

template <typename T>
T Sum(T First) {
  return First;
}

// Non-type template parameter pack.
template <int... Values>
struct IntPack {
  static const int Count = sizeof...(Values);
};

// Template template parameter pack usage (pack of types forwarded to
// another variadic template).
template <typename... Args>
struct Tuple {
  static const uint Size = sizeof...(Args);
};

template <typename... Args>
uint CountArgs(Args... args) {
  return sizeof...(Args);
}

// Pack expansion forwarding a parameter pack as a template argument list.
template <typename... Args>
uint Forward(Args... args) {
  return Tuple<Args...>::Size;
}

export
float TestVariadic() {
  float a = Sum(1.0, 2.0, 3.0);
  uint b = CountArgs(1, 2, 3, 4);
  uint c = Forward(1, 2);
  const int d = IntPack<1, 2, 3>::Count;
  return a + b + c + d;
}

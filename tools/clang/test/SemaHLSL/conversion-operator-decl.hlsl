// RUN: %dxc -Tlib_6_3 -verify -HV 202x %s

// This test verifies that dxcompiler accepts user-defined conversion operator
// declarations on HLSL record types in HLSL 202x and later. The behavior of
// using such operators is exercised by dedicated tests.

// expected-no-diagnostics

struct MyStruct {
  float4 f;

  operator float4() {
    return f;
  }
};

struct AnotherStruct {
  int x;

  operator int() {
    return x;
  }

  operator bool() {
    return x != 0;
  }
};

template<typename T>
struct TemplateStruct {
  T value;

  operator T() {
    return value;
  }
};

// An 'explicit' conversion operator is also a first-class HLSL 202x feature
// and should be accepted without an "extension" warning.
struct ExplicitStruct {
  int x;

  explicit operator int() {
    return x;
  }

  explicit operator bool() {
    return x != 0;
  }
};

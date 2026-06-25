// RUN: %dxc -Tlib_6_3 -verify -HV 2021 %s

// This test verifies that dxcompiler generates an error when defining a
// conversion operator (cast operator) in HLSL 2021. User-defined conversion
// operators are only supported in HLSL 202x and later.

struct MyStruct {
  float4 f;

  // expected-error@+1 {{user-defined conversion operators are only supported in HLSL 202x and later}}
  operator float4() {
    return 42;
  }
};

struct AnotherStruct {
  int x;

  // expected-error@+1 {{user-defined conversion operators are only supported in HLSL 202x and later}}
  operator int() {
    return x;
  }

  // expected-error@+1 {{user-defined conversion operators are only supported in HLSL 202x and later}}
  operator bool() {
    return x != 0;
  }
};

template<typename T>
struct TemplateStruct {
  T value;

  // expected-error@+1 {{user-defined conversion operators are only supported in HLSL 202x and later}}
  operator T() {
    return value;
  }
};

// 'explicit' user-defined conversion operators are also gated on HLSL 202x.
struct ExplicitStruct {
  int x;

  // expected-error@+1 {{user-defined conversion operators are only supported in HLSL 202x and later}}
  explicit operator int() {
    return x;
  }
};

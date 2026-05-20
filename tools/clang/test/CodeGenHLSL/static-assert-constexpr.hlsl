// RUN: %dxc -T cs_6_0 -E main -HV 202x %s | FileCheck %s

// Verifies that constexpr variables and functions used inside _Static_assert
// remain available for normal use and fold to constants in DXIL. _Static_assert
// is a Sema-only construct (it produces no code), but mixing it with the same
// constexpr entities that drive code generation confirms the early-evaluation
// machinery and CodeGen agree on the values.

constexpr int square(int x) { return x * x; }
constexpr int add(int a, int b) { return a + b; }

constexpr int kBase = 5;
constexpr int kSquared = square(kBase);          // 25
constexpr int kTotal = add(kSquared, square(3)); // 25 + 9 = 34

_Static_assert(kBase == 5, "kBase");
_Static_assert(kSquared == 25, "kSquared");
_Static_assert(kTotal == 34, "kTotal");
_Static_assert(square(kBase) + square(3) == kTotal, "consistency");

RWBuffer<int> buf;

[numthreads(1,1,1)]
void main(uint3 id : SV_DispatchThreadID) {
  constexpr int kLocal = add(kTotal, 1); // 35
  _Static_assert(kLocal == 35, "kLocal");
  _Static_assert(square(kLocal) == 1225, "kLocal squared");

  buf[id.x] = kLocal;
}

// CHECK: bufferStore
// The literal 35 must reach DXIL as a constant in all four lane slots.
// CHECK-SAME: i32 35, i32 35, i32 35, i32 35

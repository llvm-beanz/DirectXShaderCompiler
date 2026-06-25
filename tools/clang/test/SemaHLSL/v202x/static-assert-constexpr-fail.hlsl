// RUN: %dxc -T lib_6_3 -HV 202x -verify %s

// This test verifies that the compiler actually *evaluates* constexpr
// functions and variables when they appear inside a `_Static_assert`. Each
// assertion below is intentionally false; the compiler must detect the
// failure during semantic analysis (before code generation). If any
// `_Static_assert` were silently treated as "not a constant expression"
// or skipped, the expected diagnostics would not fire and the test would
// fail.

constexpr int kFortyTwo = 42;
constexpr float kHalf = 0.5f;
constexpr uint kMask = 0xF0u;

constexpr int square(int x) { return x * x; }
constexpr int add(int a, int b) { return a + b; }
constexpr int imul(int a, int b) { return a * b; }
constexpr int imax(int a, int b) { return a < b ? b : a; }
constexpr int iabs(int x) { return x < 0 ? -x : x; }
constexpr float fmul(float a, float b) { return a * b; }
constexpr uint band(uint a, uint b) { return a & b; }
constexpr bool lt(int a, int b) { return a < b; }

//===----------------------------------------------------------------------===//
// Constexpr variables
//===----------------------------------------------------------------------===//

_Static_assert(kFortyTwo == 43, "var-not-43"); // expected-error{{static_assert failed "var-not-43"}}
_Static_assert(kHalf == 0.25f, "half-not-quarter"); // expected-error{{static_assert failed "half-not-quarter"}}
_Static_assert(kMask == 0xFFu, "mask-not-FF"); // expected-error{{static_assert failed "mask-not-FF"}}

//===----------------------------------------------------------------------===//
// Constexpr function calls with literal arguments
//===----------------------------------------------------------------------===//

_Static_assert(square(4) == 17, "square-wrong"); // expected-error{{static_assert failed "square-wrong"}}
_Static_assert(add(2, 3) == 6, "add-wrong"); // expected-error{{static_assert failed "add-wrong"}}
_Static_assert(imul(6, 7) == 41, "imul-wrong"); // expected-error{{static_assert failed "imul-wrong"}}
_Static_assert(imax(3, 7) == 3, "imax-wrong"); // expected-error{{static_assert failed "imax-wrong"}}
_Static_assert(iabs(-9) == -9, "iabs-wrong"); // expected-error{{static_assert failed "iabs-wrong"}}
_Static_assert(fmul(2.0f, 3.0f) == 7.0f, "fmul-wrong"); // expected-error{{static_assert failed "fmul-wrong"}}
_Static_assert(band(0xFFu, 0x0Fu) == 0xF0u, "band-wrong"); // expected-error{{static_assert failed "band-wrong"}}
_Static_assert(lt(5, 2), "lt-wrong"); // expected-error{{static_assert failed "lt-wrong"}}

//===----------------------------------------------------------------------===//
// Constexpr functions with constexpr-variable arguments
//===----------------------------------------------------------------------===//

_Static_assert(square(kFortyTwo) == 0, "square-var-wrong"); // expected-error{{static_assert failed "square-var-wrong"}}
_Static_assert(add(kFortyTwo, 1) == 44, "add-var-wrong"); // expected-error{{static_assert failed "add-var-wrong"}}
_Static_assert(fmul(kHalf, kHalf) == 0.5f, "fmul-var-wrong"); // expected-error{{static_assert failed "fmul-var-wrong"}}

//===----------------------------------------------------------------------===//
// Nested constexpr calls
//===----------------------------------------------------------------------===//

_Static_assert(square(square(2)) == 15, "nested-wrong"); // expected-error{{static_assert failed "nested-wrong"}}
_Static_assert(add(square(3), imul(2, 4)) == 16, "nested-mixed-wrong"); // expected-error{{static_assert failed "nested-mixed-wrong"}}
_Static_assert(imax(square(3), square(2)) == 4, "imax-of-square-wrong"); // expected-error{{static_assert failed "imax-of-square-wrong"}}

//===----------------------------------------------------------------------===//
// Chains of constexpr variables
//===----------------------------------------------------------------------===//

constexpr int kA = square(3);           // 9
constexpr int kB = add(kA, 1);          // 10
constexpr int kC = imul(kB, kB);        // 100

_Static_assert(kA == 8, "kA-wrong"); // expected-error{{static_assert failed "kA-wrong"}}
_Static_assert(kB == 9, "kB-wrong"); // expected-error{{static_assert failed "kB-wrong"}}
_Static_assert(kC == 81, "kC-wrong"); // expected-error{{static_assert failed "kC-wrong"}}
_Static_assert(add(kA, kB) + kC == 0, "chain-sum-wrong"); // expected-error{{static_assert failed "chain-sum-wrong"}}

//===----------------------------------------------------------------------===//
// Function-body scope
//===----------------------------------------------------------------------===//

[shader("compute")]
[numthreads(1, 1, 1)]
void main() {
  _Static_assert(kFortyTwo == 0, "fn-var-wrong"); // expected-error{{static_assert failed "fn-var-wrong"}}
  _Static_assert(square(kFortyTwo) == 0, "fn-call-wrong"); // expected-error{{static_assert failed "fn-call-wrong"}}

  constexpr int local = add(kFortyTwo, 1); // 43
  _Static_assert(local == 42, "fn-local-wrong"); // expected-error{{static_assert failed "fn-local-wrong"}}
  _Static_assert(square(local) == 0, "fn-local-call-wrong"); // expected-error{{static_assert failed "fn-local-call-wrong"}}
}

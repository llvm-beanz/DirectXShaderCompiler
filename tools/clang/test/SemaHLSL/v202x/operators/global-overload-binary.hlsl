// RUN: %dxc -T lib_6_6 -HV 202x -verify %s

// HLSL 202x allows non-member (global / namespace-scope) operator overloads
// for class and enumeration types. The arithmetic, bitwise and comparison
// operators that were rejected as non-member declarations in HLSL 2021 must
// now be accepted in HLSL 202x both at declaration time and when used in an
// expression.

// expected-no-diagnostics

struct Vec2 {
  float x;
  float y;
};

Vec2 operator+(Vec2 a, Vec2 b) {
  Vec2 r = { a.x + b.x, a.y + b.y };
  return r;
}

Vec2 operator-(Vec2 a, Vec2 b) {
  Vec2 r = { a.x - b.x, a.y - b.y };
  return r;
}

Vec2 operator*(Vec2 a, Vec2 b) {
  Vec2 r = { a.x * b.x, a.y * b.y };
  return r;
}

Vec2 operator/(Vec2 a, Vec2 b) {
  Vec2 r = { a.x / b.x, a.y / b.y };
  return r;
}

bool operator==(Vec2 a, Vec2 b) { return a.x == b.x && a.y == b.y; }
bool operator!=(Vec2 a, Vec2 b) { return !(a == b); }
bool operator<(Vec2 a, Vec2 b)  { return a.x < b.x; }
bool operator>(Vec2 a, Vec2 b)  { return a.x > b.x; }
bool operator<=(Vec2 a, Vec2 b) { return a.x <= b.x; }
bool operator>=(Vec2 a, Vec2 b) { return a.x >= b.x; }

enum Color { Red = 0, Green = 1, Blue = 2 };

int operator+(Color a, Color b) { return (int)a + (int)b; }
int operator-(Color a, Color b) { return (int)a - (int)b; }
int operator&(Color a, Color b) { return (int)a & (int)b; }
int operator|(Color a, Color b) { return (int)a | (int)b; }
int operator^(Color a, Color b) { return (int)a ^ (int)b; }
bool operator==(Color a, int b) { return (int)a == b; }

namespace ns {
struct S { int v; };
S operator+(S a, S b) { S r; r.v = a.v + b.v; return r; }
} // namespace ns

export float test_binary_ops() {
  Vec2 a = { 1.0f, 2.0f };
  Vec2 b = { 3.0f, 4.0f };

  Vec2 sum = a + b;
  Vec2 diff = a - b;
  Vec2 prod = a * b;
  Vec2 quot = b / a;

  bool eq = (a == b);
  bool ne = (a != b);
  bool lt = (a < b);
  bool gt = (a > b);
  bool le = (a <= b);
  bool ge = (a >= b);

  int e = Red + Blue;
  int eb = Red | Blue;
  bool ec = (Red == 0);

  ns::S s1 = {1};
  ns::S s2 = {2};
  ns::S s3 = s1 + s2; // found via argument-dependent lookup

  return sum.x + diff.y + prod.x + quot.y +
         (eq ? 0.0f : 1.0f) + (ne ? 0.0f : 1.0f) +
         (lt ? 0.0f : 1.0f) + (gt ? 0.0f : 1.0f) +
         (le ? 0.0f : 1.0f) + (ge ? 0.0f : 1.0f) +
         (float)(e + eb + (ec ? 1 : 0) + s3.v);
}

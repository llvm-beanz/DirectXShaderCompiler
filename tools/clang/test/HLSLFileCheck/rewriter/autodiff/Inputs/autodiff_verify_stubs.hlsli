// Minimal stubs for verifying the output of `dxr -generate-differentials`
// with `dxc -verify`.
//
// The real `hlsl/ad/{fwd,bwd}` headers cannot be compiled end-to-end by `dxc`
// today (they pull in `<matrix_utils>` etc. through the HLSL preprocessor),
// so the rewriter-output regression tests instead prepend this file to the
// generated HLSL and drive `-verify` against the combined source.  Each
// declaration here is just rich enough to let the rewriter's output parse
// and type-check; bodies are intentionally trivial.

template<typename T> struct Value {
  T value;
  T derivative;
  Value<T> operator+(Value<T> b)       { Value<T> r; return r; }
  Value<T> operator-(Value<T> b)       { Value<T> r; return r; }
  Value<T> operator*(Value<T> b)       { Value<T> r; return r; }
  Value<T> operator/(Value<T> b)       { Value<T> r; return r; }
  Value<T> operator-()                 { Value<T> r; return r; }
};

template<typename T> struct Variable {
  T value;
};

// In the real library, VariableExpr<T> is a lightweight handle holding a
// reference to a Variable<T>; for verification purposes, treating it as an
// alias makes the generated `add(multiply(...), ...)` style chains type-check.
template<typename T> using VariableExpr = Variable<T>;

template<typename T> struct GradientContext {
  T accumulator;
};

// Backward-mode helpers ------------------------------------------------------

template<typename T>
VariableExpr<T> makeVariableExpr(Variable<T> v) {
  return v;
}

template<typename T> Variable<T> add(VariableExpr<T> a, VariableExpr<T> b)      { Variable<T> r; return r; }
template<typename T> Variable<T> subtract(VariableExpr<T> a, VariableExpr<T> b) { Variable<T> r; return r; }
template<typename T> Variable<T> multiply(VariableExpr<T> a, VariableExpr<T> b) { Variable<T> r; return r; }
template<typename T> Variable<T> divide(VariableExpr<T> a, VariableExpr<T> b)   { Variable<T> r; return r; }
template<typename T> Variable<T> negate(VariableExpr<T> a)                      { Variable<T> r; return r; }

template<typename T> void addAssign(inout Variable<T> a, VariableExpr<T> b) {}
template<typename T> void subAssign(inout Variable<T> a, VariableExpr<T> b) {}
template<typename T> void mulAssign(inout Variable<T> a, VariableExpr<T> b) {}
template<typename T> void divAssign(inout Variable<T> a, VariableExpr<T> b) {}

template<typename T> Variable<T> sinExpr(VariableExpr<T> a)   { Variable<T> r; return r; }
template<typename T> Variable<T> cosExpr(VariableExpr<T> a)   { Variable<T> r; return r; }
template<typename T> Variable<T> tanExpr(VariableExpr<T> a)   { Variable<T> r; return r; }
template<typename T> Variable<T> asinExpr(VariableExpr<T> a)  { Variable<T> r; return r; }
template<typename T> Variable<T> acosExpr(VariableExpr<T> a)  { Variable<T> r; return r; }
template<typename T> Variable<T> atanExpr(VariableExpr<T> a)  { Variable<T> r; return r; }
template<typename T> Variable<T> sinhExpr(VariableExpr<T> a)  { Variable<T> r; return r; }
template<typename T> Variable<T> coshExpr(VariableExpr<T> a)  { Variable<T> r; return r; }
template<typename T> Variable<T> tanhExpr(VariableExpr<T> a)  { Variable<T> r; return r; }

template<typename T> Variable<T> expExpr(VariableExpr<T> a)   { Variable<T> r; return r; }
template<typename T> Variable<T> exp2Expr(VariableExpr<T> a)  { Variable<T> r; return r; }
template<typename T> Variable<T> logExpr(VariableExpr<T> a)   { Variable<T> r; return r; }
template<typename T> Variable<T> log2Expr(VariableExpr<T> a)  { Variable<T> r; return r; }
template<typename T> Variable<T> log10Expr(VariableExpr<T> a) { Variable<T> r; return r; }
template<typename T> Variable<T> power(VariableExpr<T> a, VariableExpr<T> b) { Variable<T> r; return r; }

template<typename T> Variable<T> sqrtExpr(VariableExpr<T> a)     { Variable<T> r; return r; }
template<typename T> Variable<T> rsqrtExpr(VariableExpr<T> a)    { Variable<T> r; return r; }
template<typename T> Variable<T> rcpExpr(VariableExpr<T> a)      { Variable<T> r; return r; }
template<typename T> Variable<T> absExpr(VariableExpr<T> a)      { Variable<T> r; return r; }
template<typename T> Variable<T> saturateExpr(VariableExpr<T> a) { Variable<T> r; return r; }

template<typename T> Variable<T> minExpr(VariableExpr<T> a, VariableExpr<T> b) { Variable<T> r; return r; }
template<typename T> Variable<T> maxExpr(VariableExpr<T> a, VariableExpr<T> b) { Variable<T> r; return r; }
template<typename T> Variable<T> clampExpr(VariableExpr<T> a, VariableExpr<T> b, VariableExpr<T> c) { Variable<T> r; return r; }
template<typename T> Variable<T> lerpExpr(VariableExpr<T> a, VariableExpr<T> b, VariableExpr<T> c)  { Variable<T> r; return r; }
template<typename T> Variable<T> stepExpr(VariableExpr<T> a, VariableExpr<T> b) { Variable<T> r; return r; }
template<typename T> Variable<T> smoothstepExpr(VariableExpr<T> a, VariableExpr<T> b, VariableExpr<T> c) { Variable<T> r; return r; }
template<typename T> Variable<T> fmodExpr(VariableExpr<T> a, VariableExpr<T> b) { Variable<T> r; return r; }

// Forward-mode helpers -------------------------------------------------------
//
// The rewriter leaves intrinsic calls untranslated in forward mode, relying
// on overloads of `sin`, `cos`, ... that accept `Value<T>`.  We provide those
// overloads here so the generated code type-checks.  HLSL does not permit
// non-member operator overloads, so the arithmetic operators on `Value<T>`
// are declared inline in the struct definition above.

template<typename T> Value<T> sin(Value<T> a)  { Value<T> r; return r; }
template<typename T> Value<T> cos(Value<T> a)  { Value<T> r; return r; }
template<typename T> Value<T> tan(Value<T> a)  { Value<T> r; return r; }
template<typename T> Value<T> asin(Value<T> a) { Value<T> r; return r; }
template<typename T> Value<T> acos(Value<T> a) { Value<T> r; return r; }
template<typename T> Value<T> atan(Value<T> a) { Value<T> r; return r; }
template<typename T> Value<T> sinh(Value<T> a) { Value<T> r; return r; }
template<typename T> Value<T> cosh(Value<T> a) { Value<T> r; return r; }
template<typename T> Value<T> tanh(Value<T> a) { Value<T> r; return r; }

template<typename T> Value<T> exp(Value<T> a)   { Value<T> r; return r; }
template<typename T> Value<T> exp2(Value<T> a)  { Value<T> r; return r; }
template<typename T> Value<T> log(Value<T> a)   { Value<T> r; return r; }
template<typename T> Value<T> log2(Value<T> a)  { Value<T> r; return r; }
template<typename T> Value<T> log10(Value<T> a) { Value<T> r; return r; }
template<typename T> Value<T> pow(Value<T> a, Value<T> b) { Value<T> r; return r; }

template<typename T> Value<T> sqrt(Value<T> a)     { Value<T> r; return r; }
template<typename T> Value<T> rsqrt(Value<T> a)    { Value<T> r; return r; }
template<typename T> Value<T> rcp(Value<T> a)      { Value<T> r; return r; }
template<typename T> Value<T> abs(Value<T> a)      { Value<T> r; return r; }
template<typename T> Value<T> saturate(Value<T> a) { Value<T> r; return r; }

template<typename T> Value<T> min(Value<T> a, Value<T> b) { Value<T> r; return r; }
template<typename T> Value<T> max(Value<T> a, Value<T> b) { Value<T> r; return r; }
template<typename T> Value<T> clamp(Value<T> a, Value<T> b, Value<T> c) { Value<T> r; return r; }
template<typename T> Value<T> lerp(Value<T> a, Value<T> b, Value<T> c)  { Value<T> r; return r; }
template<typename T> Value<T> step(Value<T> a, Value<T> b) { Value<T> r; return r; }
template<typename T> Value<T> smoothstep(Value<T> a, Value<T> b, Value<T> c) { Value<T> r; return r; }
template<typename T> Value<T> fmod(Value<T> a, Value<T> b) { Value<T> r; return r; }

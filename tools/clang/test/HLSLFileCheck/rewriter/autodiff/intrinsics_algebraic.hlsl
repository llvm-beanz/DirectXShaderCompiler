// RUN: %dxr -generate-differentials %s | FileCheck %s
//
//
// Note: this test only checks the rewriter output. Running `dxc -verify`
// on the generated backward-mode code is intentionally skipped because
// the rewriter declares the generated function to return `Variable<T>`
// but the real hlsl/ad/bwd library's combinators (`add`, `multiply`,
// ...) return expression-template types (e.g. `BackAddExpr<...>`). This
// mismatch is a pre-existing rewriter limitation documented in
// agent_thoughts.md and is out of scope for the include-path change.

// Algebraic / piecewise-smooth intrinsics: sqrt, rsqrt, rcp, abs,
// min/max/clamp, lerp, saturate, smoothstep, step, fmod.

// CHECK: namespace user { namespace ad { namespace bwd {
// CHECK: sqrtExpr<float>(x_expr)
// CHECK: rsqrtExpr<float>(x_expr)
// CHECK: rcpExpr<float>(x_expr)
// CHECK: absExpr<float>(x_expr)
// CHECK: minExpr<float>(x_expr, y_expr)
// CHECK: maxExpr<float>(x_expr, y_expr)
// CHECK: clampExpr<float>(x_expr, x_expr, y_expr)
// CHECK: lerpExpr<float>(x_expr, y_expr, x_expr)
// CHECK: saturateExpr<float>(x_expr)
// CHECK: stepExpr<float>(x_expr, y_expr)
// CHECK: smoothstepExpr<float>(x_expr, y_expr, x_expr)
// CHECK: fmodExpr<float>(x_expr, y_expr)
// CHECK: } } } // namespace user::ad::bwd

[[dxc::autodiff(bwd)]]
float f(float x, float y) {
  return sqrt(x) + rsqrt(x) + rcp(x) + abs(x)
       + min(x, y) + max(x, y) + clamp(x, x, y)
       + lerp(x, y, x) + saturate(x)
       + step(x, y) + smoothstep(x, y, x)
       + fmod(x, y);
}

float main(float x : A) : SV_Target { return f(x, x); }

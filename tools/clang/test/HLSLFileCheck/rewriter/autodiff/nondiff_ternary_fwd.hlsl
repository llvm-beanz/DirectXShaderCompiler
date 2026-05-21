// RUN: %dxr -generate-differentials %s | FileCheck %s
//
// Run the rewritten output through dxc -verify and confirm that the
// generated _Static_assert fires. HLSL does not permit a parameter-less
// constructor on the templated return type, so the rewriter-emitted
// `return Value<float>();` / `return Variable<float>();` fallback also
// produces an error; we expect that one explicitly too.
//
// RUN: %dxr -generate-differentials %s > %t.gen.hlsl
// RUN: sed -E 's@(_Static_assert\(false.*)@\1 // expected-error{{static_assert failed}}@;s@(return (Value|Variable)<[^>]*>\(\).*)@\1 // expected-error{{cannot have an explicit empty initializer}}@' %t.gen.hlsl > %t.gen.ann.hlsl
// RUN: %dxc -I %hlsl_headers -T ps_6_9 -HV 2021 -verify %t.gen.ann.hlsl

// Forward-mode counterpart to nondiff_ternary.hlsl: the ternary ?: is not
// differentiable, so the forward-mode generated function is replaced by a
// _Static_assert stub with a human-readable reason.

// CHECK: namespace user { namespace ad { namespace fwd {
// CHECK: Value<float> use_cmp(Value<float> x, Value<float> y)
// CHECK: _Static_assert(false, "auto-diff cannot generate forward-mode for 'use_cmp': the ternary ?: operator is not differentiable");
// CHECK: } } } // namespace user::ad::fwd

[[dxc::autodiff(fwd)]]
float use_cmp(float x, float y) {
  return (x < y) ? x : y;
}

float main(float x : A) : SV_Target { return use_cmp(x, x); }

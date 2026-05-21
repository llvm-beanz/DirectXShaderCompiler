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
// RUN: cat %S/Inputs/autodiff_verify_stubs.hlsli %t.gen.ann.hlsl > %t.full.hlsl
// RUN: %dxc -T ps_6_0 -verify %t.full.hlsl

// Forward-mode counterpart to nondiff_if.hlsl: data-dependent control flow
// (if) is not soundly differentiable; the forward-mode function body is
// replaced by a _Static_assert stub pointing the user at [[dxc::no_diff]] or
// branchless math.

// CHECK: namespace user { namespace ad { namespace fwd {
// CHECK: Value<float> branchy(Value<float> x)
// CHECK: _Static_assert(false, "auto-diff cannot generate forward-mode for 'branchy': data-dependent control flow (if) is not differentiable; use {{\[\[}}dxc::no_diff]] or branchless math");
// CHECK: } } } // namespace user::ad::fwd

[[dxc::autodiff(fwd)]]
float branchy(float x) {
  if (x > 0)
    return x;
  return -x;
}

float main(float x : A) : SV_Target { return branchy(x); }

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

// Data-dependent control flow (if) is not soundly differentiable; the
// function body is replaced by a _Static_assert with a message pointing
// the user at [[no_diff]] / branchless math.

// CHECK: namespace user { namespace ad { namespace bwd {
// CHECK: Variable<float> branchy(inout GradientContext<float> context, Variable<float> x)
// CHECK: _Static_assert(false, "auto-diff cannot generate backward-mode for 'branchy': data-dependent control flow (if) is not differentiable; use {{\[\[}}dxc::no_diff]] or branchless math");
// CHECK: } } } // namespace user::ad::bwd

[[dxc::autodiff(bwd)]]
float branchy(float x) {
  if (x > 0)
    return x;
  return -x;
}

float main(float x : A) : SV_Target { return branchy(x); }

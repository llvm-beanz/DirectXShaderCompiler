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

// Non-differentiable intrinsics generate a _Static_assert stub with a
// human-readable reason rather than miscompiling silently.

// CHECK: namespace user { namespace ad { namespace fwd {
// CHECK: Value<float> uses_asint(Value<float> x)
// CHECK: _Static_assert(false, "auto-diff cannot generate forward-mode for 'uses_asint': bit-cast 'asint' is not differentiable");
// CHECK: } } } // namespace user::ad::fwd

// CHECK: namespace user { namespace ad { namespace bwd {
// CHECK: Variable<float> uses_asint(inout GradientContext<float> context, Variable<float> x)
// CHECK: _Static_assert(false, "auto-diff cannot generate backward-mode for 'uses_asint': bit-cast 'asint' is not differentiable");
// CHECK: } } } // namespace user::ad::bwd

[[dxc::autodiff(fwd, bwd)]]
float uses_asint(float x) { return asint(x); }

float main(float x : A) : SV_Target { return uses_asint(x); }

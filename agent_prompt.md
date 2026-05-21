---
model: claude-opus-4.7
---

# Initial Guidelines

Please make sure that your changes are appropriately tested with unit tests
covering each phase of translation in the compiler, and that your changes
conform to the [LVLM Coding Standards](llvm/docs/CodingStandards.rst).

Verify your changes by building and testing using the
cmake/caches/PredefinedParams.cmake file with CMake's -C flag to configure the
build. Test the compiler and runtime support with the targets: check-all.

Break your changes into small code changes with each change committed
spearately. Record your thought process into a file named "agent_thoughts.md" at
the root of the repository and commit it in its own commit when you're done.

# Request

I've added a new set of headers under the lib/Headers/hlsl/ad folder, which
includes an implementation for an automatic differentiation library. To make it
easier for users to use this, I'd like to add a new rewriter capability to the
dxr tool, that generates differentiable versions of functions.

To facilitate this, I would like to add a new attribute to DXC
`[[dxc::autodiff(...)]]` which supports taking arguments `fwd` or `bwd` in the
forms:

```
[[dxc::autodiff(fwd)]]
[[dxc::autodiff(bwd)]]
[[dxc::autodiff(fwd, bwd)]]
[[dxc::autodiff(bwd, fwd)]]
```

The attribute should apply only to function definitions, and should be ingored
during normal compilation.


When the rewriter is run with the `--generate-differentials` flag the rewriter
will walk the AST and find any functions annotated with new attributes.

It will then generate new functions that translate the existing function into a
version using the appropriate differntial api. Place generated functions into
either the `user::ad::fwd` or `user::ad::bwd` namespace as appropriate.

For example, if I have a function:

```
[[dxc::autodiff(fwd, bwd)]]
float f(float x) {
  return sin(x) * cos(x) + exp(x);
}
```

The rewriter should add new functions something like:
```
namespace user {
napespace ad {
namespace fwd {
Value<float> fn(Value<float> x)
{
    // Define the function: f(x) = sin(x) * cos(x) + exp(x)
    Value<float> sin_x = sin(x);
    Value<float> cos_x = cos(x);
    Value<float> sin_cos = sin_x * cos_x;
    Value<float> exp_x = exp(x);
    return sin_cos + exp_x;
}
} // namespace fwd

namespace bwd {

Variable<float> fn(GradientContext<float> context, Variable<float> x)
{
    VariableExpr<float> x_expr = makeVariableExpr<float>(x);

    // f(x) = sin(x) * cos(x) + exp(x)
    auto sin_x = sinExpr<float>(x_expr);
    auto cos_x = cosExpr<float>(x_expr);
    auto sin_cos = multiply<float>(sin_x, cos_x);
    auto exp_x = expExpr<float>(x_expr);
    return add<float>(sin_cos, exp_x);
}

} // namespace bwd
} // namespace ad
} // namespace user
```

The new functions should be added immediately after the function they are
generated based on.

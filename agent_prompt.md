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

If I apply the `[[dxc::no_diff]]` attribute to a variable all uses of that
variable should be treated as `no_diff`. For example:

```
[[dxc::no_diff]] float x0 = floor(loc.x);
if (x0 < 0)
  x0 += 2;
```

When I run the rewriter this should generate autodiff code exactly matching the
input:

```
[[dxc::no_diff]] float x0 = floor(loc.x);
if (x0 < 0)
  x0 += 2;
```

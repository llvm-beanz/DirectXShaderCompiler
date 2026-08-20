---
model: claude-opus-5
---

# Initial Guidelines

Please make sure that your changes are appropriately tested with unit tests
covering each phase of translation in the compiler, and that your changes
conform to the [LLVM Coding Standards](llvm/docs/CodingStandards.rst).

Verify your changes by building and testing using the
cmake/caches/PredefinedParams.cmake file with CMake's -C flag to configure the
build. Test the compiler and runtime support with the targets: check-all.

Break your changes into small code changes with each change committed
spearately. Record your thought process into a file named "agent_thoughts.md" at
the root of the repository and commit it in its own commit when you're done.

# Request

In DXC's gen_intrin_main.txt we define a function as:

```
void [[min_sm=6.10]] __builtin_LinAlg_MatrixLoadFromMemory(out LinAlgMatrix ret, groupshared LinAlg[] memory, in uint offset, in uint stride, in uint layout);
```

I need to be able to instead define it as:

```
void [[min_sm=6.10]] __builtin_LinAlg_MatrixLoadFromMemory(out LinAlgMatrix ret, groupshared LinAlg<>[] memory, in uint offset, in uint stride, in uint layout);
```

The difference is that I need to be able to call this function not just with an
array of arithmetic values, but also with an array of vectors of arithmetic
values.

Can you please update the code to support this case and write appropriate tests?

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

1) Can you rename the `[[no_diff]]` attribute to `[[dxc::no_diff]]`, and fix the
   tests?
2) Also add tests for `[[dxc::no_diff]]` on function calls to user functions and
   builtin functions, as well as operator expressions and variable declarations.
3) Some of the recently added tests are backward diff only, can you add forward
   versions?
4) Can you ensure that the fwd and bwd libraries have full coverage of all the
   differentiable functions in gen_intrin_main.txt?

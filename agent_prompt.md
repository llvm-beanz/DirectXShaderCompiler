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

There is an `if` statement in SemaHLSL.cpp on line 5595 disabling the `hlsl`
namespace. Can you enable that line under HLSL 202x, and write a bunch of tests
to verify that hlsl's complex built-in data types and functions get properly put
under the `hlsl` namespace under 202x and are resolved through the `hlsl` namespace?

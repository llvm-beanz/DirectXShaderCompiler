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

Can you enable `static_assert` matching C++11's declaration (requiring a
condition and an unevaluated string) and C++17's declaration (requiring just a
condition) in the HLSL 202x language mode?

Include tests that check both statically verifiable conditions (ones that are
true and ones that are false) and trigger errors for cases that aren't
verifiable.

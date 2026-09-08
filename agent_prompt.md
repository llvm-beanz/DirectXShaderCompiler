---
model: gpt-5.6-sol
---

# Initial Guidelines

Please make sure that your changes are appropriately tested with unit tests
covering each phase of translation in the compiler, and that your changes
conform to the [LLVM Coding Standards](docs/CodingStandards.rst).

Verify your changes by building and testing using the
cmake/caches/PredefinedParams.cmake cache file with CMake's -C option and
building the check-all target.

Break your changes into small code changes with each change committed
spearately. Record your thought process into a file named "agent_thoughts.md" at
the root of the repository and commit it in its own commit when you're done.

# Request

The changes in this branch have a nice start on removing support for the legacy effects syntax.

I'd like to continue working on this by adding some new optional warnings.

I've added a new diagnostic group `HLSL2026Effects`. I'd like you to add a new
set of warning diagnostics that are disabled by default, and only trigger in
HLSL 2021 and earlier which warn any time the compiler encounters effects
syntax. The new warning should note that the effects syntax is being removed in
HLSL 2026 (the compiler does not yet suppor the 2026 language mode, but it will
be based on 202x which exists in the codebase today).

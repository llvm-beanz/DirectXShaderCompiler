---
model: claude-opus-4.8
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

DXC has a whole bunch of modifications throughout the parser to allow it to ignore the legacy HLSL effects syntax. Under the HLSL 202x language mode, the effects syntax is now fully removed, meaning we should be able to disable the changes in the parser that allow DXC to ignore effects annotations and allow the compiler to generate diagnostics (errors) as it would naturally.

Can you please update DXC to remove the effects syntax support and add tests to verify that DXC produces diagnostics when it encounters effects annotations that are invalid syntax?

---
model: claude-sonnet-5
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

On incorrect assumption that the last agent's work was biased by is that DXC has
tested C++ language support. DXC disabled basically all the C++ language
support, and much of that code has been subject to bitrot. We cannot therefore
rely on Clang's testing for variadic templates to provide any real meaning to
DXC's implementation of the feature.

Given that correction, can you please extend the testing support for HLSL's new
variadic template support to cover a more comprehensive set of use cases
including template instantiations of HLSL built-in templates.

Also please be sure to test the negative cases where things aren't expected to
work, like base-class packs.

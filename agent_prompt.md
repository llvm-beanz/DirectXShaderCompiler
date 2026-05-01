# Initial Guidelines

Please make sure that your changes are appropriately tested with unit tests
covering each phase of translation in the compiler, and that your changes
conform to the [LVLM Coding Standards](docs/CodingStandards.rst).

Verify your changes by building and testing using the
cmake/caches/PredefinedParams.cmake cache file with CMake's -C option and
building the check-all target.

Break your changes into small code changes with each change committed
spearately. Record your thought process into a file named "agent_thoughts.md" at
the root of the repository and commit it in its own commit when you're done.

# Request

The goal of this branch is to rewrite the way `inout` and `out` function
arguments are implemented in DXC so that the conversions and temporaries are
represented in the AST. This re-impelemntation has caused a bunch of cascading
changes to be required, in particular matrix arguments to functions have been a
problem. You can get more context by reading the document
[here](https://github.com/microsoft/DirectXShaderCompiler/files/11602657/Revising.HLSL.out.Parameters.pdf)

Please triage and fix the test failures in this branch. Additionally, I've added
some files under the new_tests folder which contain basic code samples that can
be used to generate some new test cases. Please turn those into AST and code
generation tests and put them in appropriate places under tools/clang/tests/.

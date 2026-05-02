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

The SemaExprCXX changes you made in the last session are wrong. We want that
diagnostic. If tests are failing because it is firing, you should update the
test cases.

The whole point of this branch is that parameters should be references, so toyr
change to ParmVarDecl::updateOutParamToRefType is wrong and should be reverted.

I think we should remove all the code that elides copies at the AST-level. That
seems to be problematic and any case where it is safe to elide the copy we will
see the copies eliminated by the IR optimizer after inlining.

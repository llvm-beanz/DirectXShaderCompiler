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

I've added comments to some of the tests prefixed with the string "//
COPILOT-TODO:", can you read those comments and address the feedback? When
you're done remove the "//COPILOT-TODO:" comments.

The added diagnostics refer to `char` types. Can you also add tests to ensure
that the `char` and `unsigned char` types parse correctly and work as
alternatives to `int8_t` and `uint8_t` respectively?

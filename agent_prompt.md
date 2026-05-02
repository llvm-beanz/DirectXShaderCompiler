# Initial Guidelines

Please make sure that your changes are appropriately tested with unit tests
covering each phase of translation in the compiler, and that your changes
conform to the [LVLM Coding Standards](docs/CodingStandards.rst).

Verify your changes by building and testing using the
cmake/caches/PredefinedParams.cmake cache file with CMake's -C flag to configure
the build. Test the compiler and runtime support with the check-all target.

Break your changes into small code changes with each change committed
spearately. Record your thought process into a file named "agent_thoughts.md" at
the root of the repository and commit it in its own commit when you're done.

# Request

Some of DXC's output (particularly in tests) contains carriage returns (`\r`)
even on unix platforms that only use line feed (`\n`) newlines. Can you do a
search of DXC's code and identify places where carriage returns are explicitly
added and replace them with an appropriate portable implementation?

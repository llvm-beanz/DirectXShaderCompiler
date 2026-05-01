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

Can you now add support for SPIRV properly respecting the SPIRV extension
(https://github.khronos.org/SPIRV-Registry/extensions/KHR/SPV_KHR_8bit_storage.html)?

Since the int8 types are only supported when SM 6.10 is specified, this
shouldn't cause any of the existing SPIRV tests that define int8 using inline
spirv feature to fail. Please add appropriate tests to cover the SPIRV
generation.

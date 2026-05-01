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

I would like you to add `int8_t` and `uint8_t` support for HLSL under the 6_10
shader model profiles. The new types should generate i8 values in LLVM IR, and
work as 8-bit integer types would in C. We should also add new aliases for 2-4
element vectors (e.g. int8_t2, int8_t3, int8_t4, uint8_t2, uint8_t3, uint8_t4)

The DXIL validator will need to be updated to allow i8 types in DXIL for DXIL
1.10.

With these changes we should add testing for the frontend parsing and AST
generation, as well as code generation and DXIL validation. The testing should
cover for each expected operator (arithmetic operators, sizeof, etc), as well as
storage in different buffer formats (StructuredBuffer, cbuffer, and
ConstantBuffer). We should disallow 8-bit types in typed buffers (Buffer,
Texture, etc).

Constant buffer packing for 8-bit types should allow packing 4 8-bit values into
a 32-bit constant. When reading from constant buffers we should read a full
32-bit value and mask off any unused bits appropriately.

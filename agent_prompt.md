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

DXC has some confusing platform layers, particularly around the handling of UTF8
and various wide character formats common on Windows.

Internally DXC's Clang and LLVM-based code operates entirely on UTF-8, but at
some boundaries on Windows (specifically writing to the terminal) DXC needs to
generate 16-bit characters. Unfortunately the confusing platform layers in
dxcapi.use.cpp, DxcSupport, DxilDia, and other parts of the tooling sometimes
result in UTF-8 buffers being converted to UTF-16 and back to UTF-8.

In other places, like the WEX adapter code, we write wide-strings to terminials
that are sometimes UTF-8, which results in the strings not being printed since
the top bytes are often interpreted as null characters.

Can you inspec the DXC codebases' code that converts between UTF-8 and wide
character formats and clean up the abstraction so that strings always remain
UTF-8 until they are used in a way where they must be wide character (such as
writing them to a console, file, or output buffer)?

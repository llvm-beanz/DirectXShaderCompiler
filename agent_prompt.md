# Initial Guidelines

Please make sure that your changes are appropriately tested with unit tests
covering each phase of translation in the compiler, and that your changes
conform to the [LVLM Coding Standards](llvm/docs/CodingStandards.rst).

Verify your changes by building and testing using the
cmake/caches/PredefinedParams.cmake file with CMake's -C flag to configure the
build. Test the compiler and runtime support with the targets: check-all.

Break your changes into small code changes with each change committed
spearately. Record your thought process into a file named "agent_thoughts.md" at
the root of the repository and commit it in its own commit when you're done.

# Request

I'd like to make sure that if the -I flag points to a folder containing a header
included with the angled bracket #include (<>), the preprocessor will grab the
file from disk even if it is built into the binary. This will allow tools to
find the file on disk when possible which will allow users to inspect the header
on their system and modify it if they really want.

Can you ensure that this workflow is covered in our test cases?

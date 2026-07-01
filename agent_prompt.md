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

The code in the spirv::LowerTypeVisitor class defined in
tools/clang/lib/SPIRV/LowerTypeVisitor.cpp does a lot of string matching of data
types. This causes the compiler to mis-handle user-defined data types which may
have the same name as the built-in type regardless of the fully-qualified name
(it is legal to use these names in your own namespace).

In tools/clang/include/clang/AST/HlslTypes.h we have a bunch of functions that
expose many of the same checks but don't implement them as string compares
(except RayQuery) instead they are checked with AST annotations that the
compiler inserts to identify the built-in types.

Can you first update RayQuery to have an AST attribute to mark it as unique from
any other user-defined structure named RayQuery?

Once you've done that, in a separate set of changes can you clean up the
LowerTypeVisitor to not string match names? Please add tests that verify that
user-defined types (in namespaces) with the same names as built-in types work as
expected.

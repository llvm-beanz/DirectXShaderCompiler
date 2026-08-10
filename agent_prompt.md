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

Can you rewrite the code in CGHLSLMS.cpp to eliminate KeywordToKind and
KeywordToClass, to instead use the attributes on the record types?

Also please add an attribute to the AST to denote ROV types so that various
places throughout the code that check `startswith("RasterizerOrdered")` can be
cleaned up, and please clean those up too.

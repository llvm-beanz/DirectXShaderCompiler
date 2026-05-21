---
model: claude-opus-4.7
---

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

The new dxr -generate-differentials flag is a great start. A few changes:

1) Move the differential generaton code out into a separate source file from the
   dxcrewriteunused.cpp file. Adding more code there will make this
   unmaintainable.
2) We need to exhaustively extend the fwd and bwd implementations with all the
   HLSL intrinsics defined in gen_intrin_main.txt and the builtin operators for
   math functions that are differential.
3) We need to exhaustively extend the rewriter to support all the builtin
   operations.
4) We need exhaustive test coverage for all the points above.
5) We need to handle the failure cases, there are some operations that are not
   differentiable, for those we should generate stub functions in the rewriter
   output and put `_Static_assert(false, "...")` with a helpful message into the
   rewritten output.
6) We will need some way to mark code that is expected to not be differential in
   the translation. Can you add a statement attribute `[[no_diff]]` which, when
   present the rewriter will copy the source statement directly instead of
   making it differentiated.

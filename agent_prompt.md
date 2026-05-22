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

Let's make member functions of classes and structs differentiable. If a class
has any members marked with the autodiff attribute generate a new class in the
`user::ad::*` namespace with the same name that inherits from the user-supplied
class, and add new methods inside that class that implement the differential of
the function.

You should support the case where a user may explicitly provide the
`user::ad::*` class as empty, or with a subset of member functions declared and
the tooling should insert additional functions as needed.

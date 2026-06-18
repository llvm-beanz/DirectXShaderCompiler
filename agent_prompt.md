---
model: claude-opus-4.7
---

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

Is it possible to unify `IsHLSLNumericOrAggregateOfNumericType` and `IsHLSLIntangibleType`? It seems like one should be the opposite of the other.

Is it possible to implement `__is_scalar_layout_compatible` with the `FlattenedTypeIterator` instead of `GetHLSLFlattenedScalarTypes`? It seems like unnecessary code duplication to have both.

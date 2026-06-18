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

I want you to work on several tasks, and commit your thoughts after each task.

## Intangible types

I'd like you to implement a new type trait `__is_intangible` following the patterns in TokenKinds.def. The type trait should take a single argument which is a type name, and should evaluate to true as a constant expression if the type argument is or contains a resource type, node record type, or any other type whose size is unknown and cannot be stored to a resource or groupshared memory.

## Scalar layout compatible

I'd like you to implement a new type trait `__is_scalar_layout_compatible` which takes two types and evaluates to `true`  as a constant expression if it would be valid to cast an object of one type to the other with a "flat" cast. This requires that the objects have the same number of scalar elements, and that each corresponding element is convertible to the type of the other corresponding element.

## Builtin __bit_cast

I'd like you to implement a `__builtin_bit_cast` primitive that takes a type argument and an object. If the object is not of the size of the type or if either the object or type are intangible types it should produce an error, otherwise it should cast the value of the object to an rvalue of the destination type as a bit-cast which preserves the bit layout of the input object.

## <bit_cast>

Add a new built-in header named `bit_cast`, which implements a `hlsl::bit_cast` function template that performs a bit cast from a source type to a destination type as long as the source and destination are the same size and not intangible types.

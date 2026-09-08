---
model: gpt-5.6-sol
---

# Initial Guidelines

Please make sure that your changes are appropriately tested with unit tests
covering each phase of translation in the compiler, and that your changes
conform to the [LLVM Coding Standards](docs/CodingStandards.rst).

Verify your changes by building and testing using the
cmake/caches/PredefinedParams.cmake cache file with CMake's -C option and
building the check-all target.

Break your changes into small code changes with each change committed
spearately. Record your thought process into a file named "agent_thoughts.md" at
the root of the repository and commit it in its own commit when you're done.

# Request

I've added a few comments that start with `COPILOT-TODO` to provide feedback on
the changes in this branch. Can you address the feedback and remove the comments
when you are done?

Also review the code in this branch to see if there are other places where the
same feedback would apply, and if there are any other changes that should be
revised based on the coding standards.

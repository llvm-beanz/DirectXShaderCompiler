# Engineering Rationale: HLSL effects syntax

This file records the implementation rationale and validation decisions for the
branch. It intentionally summarizes engineering conclusions rather than private
chain-of-thought.

## Scope

Legacy effects syntax is accepted through parser recovery in HLSL 2021 and
earlier, but is rejected through normal parser and semantic diagnostics in HLSL
202x. The branch also introduces an opt-in warning for constructs that will be
removed.

The affected translation phases are:

1. Parsing effect annotations, state blocks, `sampler_state` assignments, and
   `technique` blocks.
2. Semantic registration and diagnosis of deprecated effect object types.
3. Diagnostic grouping and command-line warning control.

## Review feedback

The review comments identified duplicate diagnostics at each recovery site and
requested a parameterized, default-ignored warning. The same pattern also
appeared at the other parser recovery sites and in semantic analysis, so the
fix was applied consistently to every effects construct.

The separate parser and semantic diagnostics were replaced by one common
`warn_hlsl_2026_effects` diagnostic. Its parameters select the construct,
preserve the annotation's "possible" qualifier, and retain the state-block
initializer guidance. Every recovery site now emits exactly one warning with
no redundant HLSL-version condition.

The warning belongs to the default-ignored `HLSL2026Effects` group.
`HLSLEffectsSyntax` contains that group so the existing `-Weffects-syntax`
spelling remains a compatibility alias.

## Coding-standards review

The HLSL 202x semantic change originally retained a fixed-size object lookup
array by inserting null entries for effect types that were no longer declared.
That made the lookup representation depend on synthetic values. The map now
uses `SmallVector`, which is already the project convention in this file, and
contains only declarations that actually exist. Newly introduced local names
use LLVM-style capitalization.

Comments added by the branch were reduced where they repeated the code. The
`COPILOT-TODO` comments were removed after their feedback was applied.

## Tests

The warning test covers both library and pixel profiles in HLSL 2018 and 2021.
It verifies:

- warnings are ignored by default, even with `-Werror`;
- `-Whlsl-2026-effects` emits one warning per parser and semantic construct;
- the parameterized wording for all construct kinds.

Existing effects and matrix tests now explicitly enable `-Weffects-syntax`,
which verifies the compatibility alias. The HLSL 202x test covers natural
parser errors for all recovered syntax forms and unknown-type semantic errors
for every deprecated effect object type.

## Validation

The build directory was configured with:

```text
cmake -C cmake/caches/PredefinedParams.cmake -S . -B build-rel
```

The `check-all` target completed with 4,682 expected passes, 10 expected
failures, 33 unsupported tests, and no unexpected failures.

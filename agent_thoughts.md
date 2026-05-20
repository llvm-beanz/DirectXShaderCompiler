# Agent Thoughts: enabling `static_assert` in HLSL 202x

## Task

Enable the C++11 form (`static_assert(cond, "message")`) and the C++17
form (`static_assert(cond)`) of `static_assert` in the HLSL 202x
language mode, with tests covering true conditions, false conditions,
and non-constant conditions.

## Investigation

`static_assert` is declared as a C++11 keyword in
`tools/clang/include/clang/Basic/TokenKinds.def`:

```
CXX11_KEYWORD(static_assert         , 0)
```

The `CXX11_KEYWORD` macro maps to `KEYWORD(static_assert, KEYCXX11)`,
so the lexer only recognizes `static_assert` as `tok::kw_static_assert`
when `LangOpts.CPlusPlus11` is set. In HLSL mode that flag is not on,
so the identifier was being parsed as a plain identifier, producing
unhelpful errors like "expected parameter declarator" and "HLSL
requires a type specifier for all declarations".

The parser side was actually already prepared for the keyword to
appear in HLSL: `ParseStaticAssertDeclaration()` in
`tools/clang/lib/Parse/ParseDeclCXX.cpp` has pre-existing `HLSL Change`
guards that suppress the C11 and C++98 compatibility warnings when
HLSL is selected. And `Parser::ParseDeclaration` already dispatches on
`tok::kw_static_assert` unconditionally. So the missing piece was just
making the lexer produce `tok::kw_static_assert` in HLSL 202x mode.

`_Static_assert` (the C11 spelling) was already working in all HLSL
versions because it is registered as a `KEYALL` keyword.

## Approach

Two surgical changes:

1. **Keyword registration**
   `tools/clang/lib/Basic/IdentifierTable.cpp`: in
   `IdentifierTable::AddKeywords`, after the existing manual keyword
   additions (`__unknown_anytype`, `__declspec`), conditionally register
   `static_assert` as `tok::kw_static_assert` when
   `LangOpts.HLSL && LangOpts.HLSLVersion >= hlsl::LangStd::v202x`.

   I considered alternatives:
   * Adding a new `KEYHLSL202X` flag in `TokenKinds.def` and
     `IdentifierTable.cpp`. Cleaner conceptually, but pollutes a file
     that is shared with upstream LLVM and adds a flag for a single
     keyword. Rejected.
   * Letting `getKeywordStatus` enable all `KEYCXX11` keywords in
     HLSL 202x. Too broad – it would also pull in `constexpr`,
     `decltype`, `nullptr`, `noexcept`, `thread_local`, `alignas`,
     etc., which are separate language-design decisions.
   * Adding a special case in `getKeywordStatus`. Slightly less
     explicit than a direct `AddKeyword` call alongside the other
     special-cased keywords (`__unknown_anytype`, `__declspec`).
     Rejected.

   The direct `AddKeyword` approach mirrors the precedent already in
   that function and is the smallest possible change.

2. **C++17 no-message form**
   In `ParseStaticAssertDeclaration` the no-message path was either
   emitting an extension warning (`ext_static_assert_no_message`,
   InGroup `<CXX1z>`) or a `cxx14_compat` warning, depending on
   whether `CPlusPlus1z` was set. Since `LangOpts.CPlusPlus1z` is
   never set in HLSL mode, an HLSL 202x user writing
   `static_assert(cond);` would get the extension warning. Treat
   HLSL 202x the same as C++1z for this purpose.

## Tests

`tools/clang/test/SemaHLSL/v202x/static-assert.hlsl`:

* True conditions at file, function-body, and class member scope
  (both C++11 and C++17 forms) — `expected-no-diagnostics` for these
  lines (rest of file uses positive `expected-*` directives so the
  test as a whole does not use the no-diagnostics directive).
* False conditions, with and without message: expect the
  `static_assert failed` error from
  `diag::err_static_assert_failed`.
* Non-constant conditions (cbuffer-backed global, function parameter):
  expect `static_assert expression is not an integral constant
  expression` plus the corresponding notes.

`tools/clang/test/SemaHLSL/v2021-static-assert-not-keyword.hlsl`:

* Verifies that in HLSL 2021 (`-HV 2021`) the identifier
  `static_assert` is *not* a keyword: the parser falls through to the
  generic declaration path and produces the expected stack of errors.
  This guards against accidentally enabling the keyword in earlier
  language modes.

## Verification

* `ninja dxc` – clean build.
* `ninja check-clang-semahlsl` – 261 expected passes, 1 expected
  failure, 0 unexpected failures.
* `ninja check-all` – 4606 expected passes, 9 expected failures, 33
  unsupported, 0 unexpected failures.

## Commits

1. *[HLSL 202x] Enable C++11/C++17-style static_assert* — the
   IdentifierTable and ParseDeclCXX changes.
2. *[HLSL 202x] Tests for static_assert in HLSL 202x* — the lit tests.
3. This file.

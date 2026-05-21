# Agent Thoughts: `[[dxc::autodiff]]` attribute and `dxr --generate-differentials`

This document records the design decisions and the non-obvious wrinkles I
hit while implementing the autodiff attribute and the matching rewriter
pass.  The work is split across four small commits, plus this notes
commit:

1. `[hlsl] Add [[dxc::autodiff(...)]] attribute`
2. `[dxr] Add -generate-differentials option`
3. `[dxr] Implement -generate-differentials rewriter pass`
4. `[test] Add FileCheck tests for [[dxc::autodiff]] and
   -generate-differentials`

## Phase 1 — the attribute

The attribute is declared in `tools/clang/include/clang/Basic/Attr.td`
as a C++11-style `dxc::autodiff` spelling with two `IdentifierArgument`s
(the second optional) and `Subjects = SubjectList<[Function]>`.  Sema
validates each argument is the identifier `fwd` or `bwd` and disallows
the same mode being passed twice.  The attribute is "ignored during
normal compilation" simply by virtue of having no codegen handling —
every backend just sees an `HLSLAutoDiffAttr` hanging off the
`FunctionDecl` and does nothing with it.

Three things were not at all obvious:

1. **HLSL turns off `LangOpts.CPlusPlus11`.**  The TableGen-generated
   `hasAttribute()` function (in `AttrHasAttributeImpl.inc`) gates almost
   every C++11 attribute on `LangOpts.CPlusPlus11`, so the HLSL parser
   silently dropped the attribute's argument list and reported "takes at
   least 1 argument".  The fix is to teach
   `hasCXXAttributeInHLSL` (in `tools/clang/lib/Parse/ParseDeclCXX.cpp`)
   that `dxc::autodiff` exists.  Without this the rest of the
   implementation is dead code.

2. **The parser only handled identifier args in the first position.**
   `ParseAttributeArgsCommon` calls `attributeHasIdentifierArg(AttrName)`
   once, before the loop, and uses that to decide *only the first*
   argument's parsing strategy.  Later arguments always went through
   `ParseAssignmentExpression`, which on HLSL refuses bare identifiers
   like `bwd`.  I generalised the do-while loop in `ParseDecl.cpp` to
   attempt `ParseIdentifierLoc()` at every position when the attribute
   opts into identifier args and the lookahead is `,` or `)`.  The
   change is backwards compatible because both conditions must hold.

3. **The auto-generated `printPretty` for an attribute with an optional
   `IdentifierArgument` is unsafe.**  TableGen emits code that
   unconditionally dereferences `getMode2()`, so a single-argument
   `[[dxc::autodiff(fwd)]]` SIGSEGV's whenever someone re-prints the
   AST — including the rewriter's `TranslationUnitDecl::print`.  Two
   things have to happen to dodge this:
   - Add a `HLSLAutoDiff` case to `CustomPrintHLSLAttr`
     (`SemaHLSL.cpp`) so we emit the textual form ourselves.
   - Add `HLSLAutoDiff` to `IsHLSLAttr`.  Without that, the generic
     `DeclPrinter::prettyPrintAttributes` path still calls the unsafe
     TableGen `printPretty`.  Only the HLSL-attribute filter routes the
     attribute through `PrintHLSLPreAttr` → `CustomPrintHLSLAttr`.

## Phase 2 — the option

`-generate-differentials` is plumbed through the standard rewriter-option
machinery: a new flag in `HLSLOptions.td`, a new `bool
GenerateDifferentials` member of `RewriterOpts` in `HLSLOptions.h`, and a
`hasFlag` call in `HLSLOptions.cpp`.  No surprises.

## Phase 3 — the rewriter

The rewriter lives in `tools/clang/tools/libclang/dxcrewriteunused.cpp`
and is gated by `opts.RWOpt.GenerateDifferentials` in `DoSimpleReWrite`.
Rather than use `clang::Rewriter` (which would have required a
source-faithful preserving printer that the existing rewriter pipeline
does not provide), I added a small custom translation-unit printer that
iterates `tu->decls()`, prints each one with the standard
`Decl::print`, and appends a generated block immediately after any
`FunctionDecl` carrying `HLSLAutoDiffAttr`.  This preserves the
"immediately after" placement that the prompt asked for and slots
cleanly into the existing `tu->print()` style used by the surrounding
rewriter modes.

The generated block is produced by `AutoDiffEmitter`, a small recursive
descent over the function body that handles:

  - Arithmetic `BinaryOperator` (`+ - * /`).  Forward mode keeps the
    HLSL operators on `Value<T>`; backward mode lowers each to
    `add/subtract/multiply/divide<T>(...)`.
  - `UnaryOperator(-)`.  Forward keeps `-x`; backward emits
    `negate<T>(x)`.
  - `CallExpr` for the AD intrinsics `sin/cos/exp/log/log2/sqrt/pow/
    max/min`.  Forward keeps the call as-is on `Value<T>`; backward
    emits the matching `sinExpr/cosExpr/expExpr/logExpr/log2Expr/
    sqrtExpr/power/maxExpr/minExpr<T>` builder.
  - `DeclRefExpr` to parameters.  Forward leaves them alone; backward
    references the `<name>_expr` variable created at the top of the
    body.
  - Integer / floating-point literals.

Anything outside this subset emits a `/* TODO: unsupported expr */`
sentinel rather than failing the rewrite — the prompt described this as
a starting point, not a complete IL.

The element type used to parameterise `Value<T>` / `Variable<T>` is
extracted from the function's return type, printed canonically.  This
has only been validated on scalar `float`; vectors and matrices will
emit something but it has not been checked end-to-end.

## Phase 4 — tests

Five FileCheck tests live alongside the existing test suite:

- `hlsl/functions/autodiff_attr.hlsl` — smoke test that all four legal
  forms of the attribute compile cleanly on a function definition and
  do not perturb DXIL emission.
- `hlsl/diagnostics/errors/autodiff_attr_errors.hlsl` — argument-count,
  identifier-vs-expression, too-many-arguments, and wrong-subject
  diagnostics.
- `rewriter/autodiff_fwd.hlsl`, `autodiff_bwd.hlsl`,
  `autodiff_fwd_bwd.hlsl` — assert the structure and body of the
  generated `user::ad::fwd` / `user::ad::bwd` namespaces.

One small wrinkle: the in-tree BatchHLSL runner does not support `RUN:
not %dxc`, only `%dxc | FileCheck`.  FileCheck's `consumeErrors` flag
makes the pipeline tolerant of `dxc` exiting nonzero, so the diagnostics
test is written without `not`.

## Known limitations

- Element-type extraction is canonical-print of the return type, which
  is correct for scalar `float` and likely wrong for `float3`,
  matrices, or user types.
- `PrintTranslationUnitWithDifferentials` only iterates the top-level
  `tu->decls()`.  Functions nested inside `namespace`s carrying the
  attribute will be ignored.  This matches the example in the prompt
  but is worth knowing.
- Whether the consumer of the rewritten file can find
  `hlsl/ad/{fwd,bwd}` on its include path is the consumer's problem;
  the rewriter does not synthesise an `#include`.

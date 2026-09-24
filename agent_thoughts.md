# Agent Thoughts: HLSL Control-Flow Uniformity Analysis

This document records the design rationale, tradeoffs, and known
limitations of the new HLSL control-flow uniformity analysis, in the
spirit of the design notes that accompany Clang's existing
`UninitializedValues` analysis.

## Goal

HLSL programs running on a GPU execute in "waves"/"warps" of SIMD lanes,
and compute shaders further group waves into thread groups. Certain
operations - most notably `GroupMemoryBarrierWithGroupSync`,
`AllMemoryBarrierWithGroupSync`, `DeviceMemoryBarrierWithGroupSync`, and
`Barrier()` with the `GROUP_SYNC` flag - are only well-defined if *every*
invocation in the thread group executes them. If such a barrier is placed
inside a branch whose condition can evaluate differently across
invocations (a "non-uniform" or "divergent" condition), some invocations
may call the barrier while others never reach it, which is undefined
behavior and a classic, hard-to-debug class of GPU deadlock/hang bugs.

This analysis performs a conservative, intraprocedural static check: for
every call to a "requires uniform control flow" operation, determine
whether it is reachable only through control flow that provably depends
on a non-uniform value, and if so, emit a warning pointing at both the
call and the offending branch condition.

## High-level approach

The analysis is modeled directly on
`tools/clang/lib/Analysis/UninitializedValues.cpp`: it is a forward
dataflow analysis over Clang's `CFG` for a single function body, invoked
from `Sema::AnalysisBasedWarnings::IssueWarnings` alongside the existing
uninitialized-variable analysis, and reports diagnostics via a handler
interface (`HLSLUniformityHandler`) so that the core analysis in
`libclangAnalysis` has no dependency on Sema.

The algorithm has three phases:

1. **Taint dataflow.** Track, per local variable/parameter, whether its
   value may be non-uniform across the thread group ("tainted"). Sources
   of taint are:
   - Parameters (or struct fields, recursively) annotated with a
     system-value semantic that is inherently per-invocation, e.g.
     `SV_DispatchThreadID`, `SV_GroupThreadID`, `SV_GroupIndex`,
     `SV_VertexID`, `SV_InstanceID`, `SV_PrimitiveID`,
     `SV_OutputControlPointID`, `SV_GSInstanceID`, `SV_SampleIndex`,
     `SV_IsFrontFace`, `SV_ViewID`, `SV_Position`.
   - Calls to `WaveGetLaneIndex()`, `WaveIsFirstLane()`, `WaveMatch()`,
     any `WavePrefix*`/`WaveMultiPrefix*` op, and
     `NonUniformResourceIndex()` - these are defined to produce a result
     that depends on the calling lane's identity, so they are always
     non-uniform regardless of their operands.
   - Any other call whose result depends on a tainted argument (taint
     propagates through generic expressions - binary/unary operators,
     casts, member access, etc. - by simple OR of the operand taints).

   Conversely, wave *reduction/broadcast* intrinsics
   (`WaveActiveSum/Product/Min/Max/BitAnd/Or/Xor/CountBits/AllEqual/
   AllTrue/AnyTrue/Ballot`, the `U` variants, `WaveGetLaneCount`, and
   `WaveReadLaneFirst`) are defined to produce the *same* value in every
   active lane of the wave, so they are treated as always uniform,
   irrespective of their arguments' taint. This matters in practice:
   patterns like `if (WaveActiveAllTrue(cond)) { barrier(); }` are a
   common and *correct* idiom for uniformly gating a barrier on a
   non-uniform condition, and must not be flagged.

   `WaveReadLaneAt` is deliberately left out of both special-case lists:
   its result's uniformity legitimately depends on whether its lane-index
   argument is uniform, so generic argument-taint propagation is already
   the right behavior for it.

   The dataflow is a simple "chaotic iteration" (repeat until no bit
   changes) over CFG blocks rather than a worklist algorithm, since HLSL
   function bodies/CFGs are small (a handful to a few hundred blocks) and
   the extra engineering complexity of a worklist is not justified. An
   iteration cap of `NumBlocks*4 + 16` guards against accidental
   non-termination bugs.

2. **Divergent region identification.** For every branching CFG block
   whose terminator condition is found to be tainted (via the dataflow
   above, or directly if the condition expression itself is/contains a
   non-uniform-source call), we compute its "divergent region": the set
   of blocks that are provably only reachable, from function entry,
   through that branch, and which have not yet reconverged with the
   "other side" of the branch. Concretely:
   - Build a forward `DominatorTree` over the CFG (reusing
     `clang::DominatorTree`, a thin wrapper Clang already provides around
     `llvm::DominatorTreeBase<CFGBlock>`).
   - Build a post-dominator tree over the same CFG. Clang does not
     provide a ready-made wrapper for this, but the necessary
     `GraphTraits<Inverse<CFG*>>`/`GraphTraits<Inverse<CFGBlock*>>`
     specializations already exist in `CFG.h`, so a post-dominator tree
     can be built directly: `llvm::DominatorTreeBase<CFGBlock> PDT(true);
     PDT.recalculate(*cfg);`.
   - For a divergent branch block `B`, let `IPDom` be its immediate
     post-dominator (the nearest block through which every path from `B`
     to the function exit must pass - i.e. the branch's reconvergence
     point). A block `X` is inside `B`'s divergent region iff `B`
     dominates `X` (`X` is only reachable through `B`) and `IPDom` does
     *not* dominate `X` (i.e. `X` executes strictly before the branch has
     reconverged).

   **Important bug found and fixed during development:** the natural
   first instinct is to test "has this block already passed the merge
   point" using post-dominance (`PDT.dominates(IPDom, X)`), reasoning
   that "every path from `X` to the exit passes through `IPDom`, so `X`
   must come after it". This is wrong: post-dominance says nothing about
   whether `X` is reachable *before* `IPDom` on any given path from
   entry - in fact almost every predecessor of `IPDom` also satisfies
   `PDT.dominates(IPDom, X)` trivially (their only path to the exit is
   through `IPDom`), which caused the algorithm to falsely exclude blocks
   that were still legitimately inside the divergent region. The correct
   test requires the **forward** dominator tree:
   `DT.dominates(IPDom, X)` asks "does every path from entry to `X` pass
   through `IPDom`", which is precisely "has flow already reconverged by
   the time we reach `X`". This was caught by manual testing with
   `llvm::errs()` tracing (see the debugging note below) and is worth
   remembering as a general trap when mixing dominance and
   post-dominance reasoning.

   For loops, the loop header's own back-edge-driven branch (e.g. the
   `for`/`while` condition block) is handled the same way as any other
   branch: if the condition is tainted, everything dominated by the
   condition block up to its immediate post-dominator (i.e., the loop
   body and anything after it that hasn't reconverged, which in a
   single-exit loop is normally just the loop body itself) is considered
   divergent. This naturally flags a barrier placed unconditionally
   inside a loop whose trip count depends on a non-uniform value (e.g.
   `for (uint i = 0; i < dtid.x; i++) { barrier(); }`), which is exactly
   the class of bug this analysis is meant to catch, since a
   non-uniform loop bound means lanes can exit the loop (and thus stop
   calling the barrier) at different iterations.

3. **Reporting.** For each call site invoking a function classified as
   requiring uniform control flow, walk the (small, statically bounded)
   set of enclosing divergent regions computed above and pick the
   *innermost* one (the one whose branch block is "closest" to the call,
   i.e. whose region is a subset of all the others enclosing the call).
   This keeps the diagnostic focused on the most specific, actionable
   branch rather than reporting every enclosing branch redundantly for
   deeply nested code.

## Classifying "requires uniform control flow" operations

- `GroupMemoryBarrierWithGroupSync`, `AllMemoryBarrierWithGroupSync`, and
  `DeviceMemoryBarrierWithGroupSync` unconditionally require uniform
  control flow, matching their documented semantics.
- The newer generic `Barrier()` intrinsic takes a semantic-flags bitmask
  argument; it only requires uniform control flow if the `GROUP_SYNC`
  flag (`hlsl::DXIL::BarrierSemanticFlag::GroupSync`) is set. If the
  argument is a compile-time constant, we check the bit directly; if it
  is *not* a provable constant, we conservatively assume the flag may be
  set (favoring false positives over silently missing real bugs, matching
  the general philosophy of static analyses like Clang's own
  uninitialized-value warnings).

## Sources of non-uniformity

The list of "non-uniform" system-value semantics
(`isNonUniformSemanticName`) is intentionally conservative and
non-exhaustive: it includes semantics that are unambiguously
per-invocation identifiers or per-invocation varying values
(`SV_DispatchThreadID`, `SV_GroupThreadID`, `SV_GroupIndex`,
`SV_VertexID`, `SV_InstanceID`, `SV_PrimitiveID`,
`SV_OutputControlPointID`, `SV_GSInstanceID`, `SV_SampleIndex`,
`SV_IsFrontFace`, `SV_ViewID`, `SV_Position`), matched case-insensitively
via `hlsl::UnusualAnnotation`/`hlsl::SemanticDecl`. This list could be
extended in the future (e.g. additional ray tracing or mesh shader
system values) without changing the shape of the analysis.

For struct-typed parameters, if *any* nested field (recursively) carries
a non-uniform semantic, the *entire* parameter is conservatively treated
as a non-uniform source. This is coarse-grained (a struct with one
non-uniform field and nine uniform fields is entirely tainted) but avoids
the complexity of field-sensitive dataflow, which did not seem justified
given the size/complexity of typical HLSL entry-point input structs.

## Known limitations / false positive & false negative tradeoffs

- **No interprocedural analysis.** Calls to user-defined (non-intrinsic)
  functions conservatively propagate taint as the OR of their argument
  taints, with no summary of what the callee actually does internally.
  This can cause:
  - **False negatives**: a helper function that reads a *global*
    non-uniform-tainted variable (rather than being passed one as an
    argument) will not be recognized as returning a tainted value.
  - **False positives**: a helper function passed a tainted argument
    that it doesn't actually use to influence the value it returns (or
    to guard a barrier internally) will make its result look tainted
    even though it isn't.
  A full interprocedural solution (e.g. per-function summaries, computed
  in a bottom-up call-graph order) would remove both classes of error,
  but the codebase does not currently have interprocedural dataflow
  infrastructure for HLSL analyses, and building one was judged out of
  scope for this change; the intraprocedular, call-site-conservative
  approach is a reasonable, precedented tradeoff (Clang's own
  `-Wuninitialized` has similar intraprocedural-only limitations).
- **Approximate divergent-region computation.** The dominator/
  post-dominator based "immediate reconvergence point" approximation is
  precise for structured, single-entry/single-exit control flow (the
  overwhelming majority of real HLSL shaders), but can be imprecise for
  pathological control flow with multiple exits out of a region (e.g.
  early `return`s from inside a divergent `if`), since the immediate
  post-dominator may end up being the function exit block rather than a
  true merge point. In such cases the analysis still correctly treats
  everything from the branch down to the (further away) post-dominator
  as divergent, so it errs toward over-approximating divergence (more
  true positives, at the cost of occasionally reporting a call as
  divergent when it is, in fact, guarded correctly through some
  unusual control-flow shape the analysis can't yet reason about
  precisely). This is the conservative direction to err in for a
  correctness-oriented warning about a hard-to-debug hang/deadlock bug
  class.
- **No support for cross-thread-group reasoning beyond the semantics
  list.** E.g., a value that happens to be uniform in practice due to
  application-specific invariants not visible to the compiler (such as
  "this cbuffer value is set to the same thing as `SV_GroupIndex` divided
  by a constant, so it's actually uniform if the constant equals the
  group size") will not be recognized as uniform; this is an inherent
  and expected limitation of any static, syntax-driven analysis of this
  kind.
- **Chaotic-iteration dataflow, not a worklist algorithm.** Chosen for
  simplicity given the expected small size of HLSL CFGs; if profiling
  ever showed this analysis to be a compile-time bottleneck on
  pathologically large shader functions, switching to a proper worklist
  algorithm (as `UninitializedValues.cpp` itself does) would be a
  natural, backward-compatible improvement.

## Debugging notes (how correctness was validated)

During development, a run with `bin/dxc` on a test file with the classic
`if (dtid.x < 32) { GroupMemoryBarrierWithGroupSync(); }` bug pattern
initially produced *no visible warning* on stderr from a plain CLI
invocation - even though the same was true for other, pre-existing,
known-working warnings (e.g. implicit truncation, uninitialized `out`
parameters) tested the same way in this build/environment. This turned
out to be an artifact of how `bin/dxc`'s plain-CLI diagnostic-output path
in this particular build's `dxcompilerobj.cpp`/`dxc.cpp` gates console
warning output (via `Opts.OutputWarnings` /
`compiler.getDiagnostics().setIgnoreAllWarnings`), not a bug in the
analysis. The reliable, idiomatic way to validate Sema diagnostics in
this codebase - used throughout the existing test suite, including for
`UninitializedValues` itself - is `-verify` mode
(`%dxc ... -verify %s` with `expected-warning`/`expected-note` comments),
which drives Clang's `VerifyDiagnosticConsumer` and is unaffected by the
console-output gating. Using `-verify`, the analysis was confirmed to:
(a) pass silently when the expected-diagnostic comments matched what was
actually emitted, and (b) fail loudly (non-zero exit, printing the exact
unexpected diagnostics) when they didn't - including a deliberate
sanity-check where the `expected-note`/`expected-warning` comments were
temporarily removed from the loop-based test case to confirm the
diagnostic really does fire for that case and the test isn't vacuously
passing.

A genuine bug was found and fixed this way: the divergent-region
"already reconverged" check was initially implemented with post-dominance
instead of forward dominance (see the "Important bug found and fixed"
note above), which caused real divergent call sites to be incorrectly
excluded from their enclosing branch's divergent region. This was caught
via targeted `llvm::errs()` tracing added temporarily during development
(and removed before the final commits) that printed, for a known-bad test
case, which blocks were being classified as inside vs. outside the
divergent region, which made the dominance/post-dominance mixup visible.

## Testing

- `tools/clang/test/SemaHLSL/nonuniform-control-flow.hlsl` is a new
  `-verify`-based lit test exercising: true positives via
  `SV_DispatchThreadID`, `SV_GroupIndex`, and `WaveGetLaneIndex()` as
  non-uniform branch conditions; true negatives for a uniform
  (cbuffer-derived) branch condition and for no branch at all; a true
  negative confirming `WaveActiveAllTrue` (a uniformizing wave op) used
  as a branch condition does not trigger a false positive; a nested-`if`
  case confirming only the innermost enclosing divergent branch is
  reported per call site; and a loop-based divergence case
  (`for (...; i < dtid.x; ...)`).
- The full existing `SemaHLSL` lit test suite (283 tests, including
  `wave.hlsl` which exercises most wave intrinsics extensively) was
  re-run after these changes and shows zero regressions.
- A broader sweep of 146 tests under `HLSLFileCheck/hlsl/intrinsics/
  barrier`, `HLSLFileCheck/hlsl/intrinsics/wave`,
  `HLSLFileCheck/hlsl/types/modifiers/groupshared`, and
  `HLSLFileCheck/hlsl/control_flow` was also run. 17 of those tests fail
  both with and without this change (verified by stashing all changes,
  rebuilding, and re-running the identical failing subset), confirming
  they are pre-existing environment/baseline failures unrelated to this
  analysis (they appear to stem from diagnostics being written to stderr
  in this build while their `RUN` lines pipe only stdout into
  `FileCheck`).

## Follow-up ideas (not implemented here)

- Interprocedural summaries for user-defined function taint/uniformity,
  to reduce both false positives and false negatives across function
  call boundaries.
- Extending the non-uniform-semantics list to cover additional shader
  stages (mesh/amplification shader system values, ray tracing system
  values reachable from closest-hit/any-hit shaders, etc.) as needed.
- A finer-grained, field-sensitive taint model for struct-typed
  parameters, instead of the current coarse "any field tainted taints the
  whole struct" approximation.

## Addendum: extending the analysis to quad uniformity

A follow-up request asked for the analysis to also understand "quad
uniformity" and the quad/derivative operations: `ddx`/`ddy` (and their
`_coarse`/`_fine` variants) and the explicit `Quad*` intrinsics
(`QuadReadAcrossX`, `QuadReadAcrossY`, `QuadReadAcrossDiagonal`,
`QuadReadLaneAt`, `QuadAny`, `QuadAll`). Unlike
`GroupMemoryBarrierWithGroupSync`, which requires every invocation of the
*entire* thread group/wave to execute it together, these operations only
require that the *four* invocations making up a single 2x2 "quad" execute
them together; it is perfectly fine for the decision to differ between
different quads. A value that is uniform across the whole group is
trivially uniform within any single quad, but the converse does not hold,
so this is a strictly weaker (and therefore distinct) requirement that
needed its own tracking rather than being folded into the existing
group-uniformity bit.

### Design: a second, scope-parameterized dataflow

Rather than inventing a wholly separate analysis, the existing
"taint"/divergent-region machinery was generalized to be parameterized by
a new `HLSLUniformityRequirement` enum (`Group` or `Quad`), and the driver
now runs the *same* dataflow and divergent-branch identification code
twice, once per scope, producing two independent `BlockOut` bitvector
sets and two independent `DivergentBranches` lists. This was a deliberate
simplicity/precision tradeoff: a fully unified "lattice of scopes" design
(e.g. tracking a per-variable "coarsest scope at which this value is
uniform" rather than two separate booleans) would avoid doing the
chaotic-iteration fixpoint work twice, but for the expected small size of
HLSL CFGs this is not a performance concern, and duplicating the
scope-parameterized functions (`isExprNonUniform`, `transferStmt`,
`analyzeScope`) is far simpler to reason about and review than a lattice
abstraction would have been.

The scope-sensitive pieces are:

- `producesGroupUniformResult`: unchanged from before (the wave
  broadcast/reduction intrinsics). Since group-uniform trivially implies
  quad-uniform, these ops are treated as uniform at *both* scopes.
- `producesQuadUniformResult`: the new list of `Quad*`
  broadcast/reduction intrinsics. Their result is uniform *within* a
  quad by construction (all four lanes read/compute the same value), but
  is **not** treated as uniform at group scope, because the value
  legitimately differs from one quad to the next (e.g.
  `QuadReadAcrossX(x)` broadcasts `x`'s value from one specific lane of
  the current quad, which generally differs between quads). This
  distinction is exactly what lets the analysis correctly warn when a
  `Quad*` result is used to guard a `GroupMemoryBarrierWithGroupSync`
  (still unsafe: the branch can differ from quad to quad) while *not*
  warning when the same `Quad*` result guards a `ddx`/`ddy` (safe: all
  four lanes of any given quad agree on the branch).
- `alwaysProducesNonUniformResult`: unchanged, and applied identically at
  both scopes. Per-lane-identity intrinsics like `WaveGetLaneIndex`
  generally differ even between the four lanes of a single quad (a
  quad's four lanes are simply four particular wave lanes with different
  indices), so there was no principled way to treat these as "quad-safe
  but group-unsafe" -- they are conservatively non-uniform everywhere.
- Non-uniform *source* semantics (`SV_Position`, `SV_DispatchThreadID`,
  etc.) are also used unchanged as taint sources for both scopes: e.g.
  `SV_Position` legitimately varies between the four pixels of a quad
  (they are adjacent but distinct screen positions), so it is correctly a
  quad-non-uniform source too, not just a group-non-uniform one.

`requiresUniformControlFlow` now returns
`Optional<HLSLUniformityRequirement>` instead of `bool`: barriers
(`GroupMemoryBarrierWithGroupSync` et al. and `Barrier()` with
`GROUP_SYNC`) require `Group`; `ddx`/`ddy` and the `Quad*` intrinsics
require `Quad`. The driver's per-block reporting loop computes the
innermost divergent branch under *both* scopes once per block, and then,
for each call site found in that block, picks whichever of the two
precomputed answers matches that specific call's own requirement -- so a
single block can correctly report a barrier against a group-divergent
branch and a `ddx` against a (possibly different, more/less nested) quad-
divergent branch in the same pass.

The diagnostic-facing `HLSLUniformityHandler::handleNonUniformControlFlowUse`
callback gained a third parameter carrying the `HLSLUniformityRequirement`,
and `Sema`'s reporter (`HLSLUniformityDiagReporter`) uses it to select
between two new, separately-grouped diagnostics
(`warn_hlsl_nonuniform_control_flow` for `Group`,
`warn_hlsl_nonuniform_quad_control_flow` for `Quad`) with wording that
says "thread group" vs. "current quad" respectively; the underlying note
diagnostic was parameterized the same way. `-Whlsl-nonuniform-quad-
control-flow` is a new diagnostic group, made a sub-group of the existing
`-Whlsl-nonuniform-control-flow` (so disabling the parent group disables
both, matching the pre-existing convention used elsewhere in this
diagnostics file, e.g. `HLSL2026Compat`).

### Two latent bugs found while extending the analysis

Manual verification of the quad extension surfaced two bugs in the
*original* group-only implementation that had gone unnoticed because the
existing test suite never happened to exercise the affected code paths.
Both are fixed as part of this change, since they directly undermine the
correctness of the very taint-propagation code being extended (a
"uniform result" intrinsic's result, once stored into a local variable,
was not reliably recognized as uniform -- which is precisely the pattern
used by the new `Quad*`-broadcast-to-a-variable test case):

1. **`SmallBitVector` has no `set(unsigned Idx, bool Value)` overload.**
   The pre-existing code wrote `Env.set(*Idx, SomeBool)` in three places
   (assignment, `DeclStmt` initialization, increment/decrement) intending
   to set-or-clear a single bit. `llvm::SmallBitVector` does not declare
   that overload; instead, `bool` implicitly converts to `unsigned`, and
   the call silently resolves to the *unrelated* range-set overload
   `set(unsigned I, unsigned E)` ("set all bits in `[I, E)`"). When the
   intended value was `false`/`0` and `Idx > 0` (the overwhelmingly
   common case), this passes `I > E` to a function documented and
   asserted (in debug builds only) to require `I <= E`; in a release
   (assertions-disabled) build this silently computes a nonsensical,
   effectively-wrapped-around bitmask and corrupts far more bits than
   intended. This was discovered by writing a minimal repro
   (`v = WaveReadLaneFirst(dtid.x); if (v < 10) { GroupMemoryBarrierWith
   GroupSync(); }`, which should never warn since `WaveReadLaneFirst`'s
   result is group-uniform by definition) and observing a false-positive
   warning; targeted `llvm::errs()` tracing of each block's taint-bit
   count confirmed the stored bitvector had more bits set than the
   number of tracked variables should allow for that program. Confirmed
   present on the pre-quad-uniformity code too (via `git stash`), so it
   predates this change and was not introduced by it. Fixed by
   introducing a small `setTaintBit(Env, Idx, Value)` helper that uses
   the correct single-bit `set(unsigned)`/`reset(unsigned)` overloads,
   and replacing all three call sites.
2. **Double-counting calls that straddle a temporary-object `CFGStmt`.**
   Clang's `CFG` builder sometimes emits a `CFGStmt` for a temporary-
   binding sub-expression (e.g. the `float4(...)` constructor call inside
   `result = float4(ddx(pos.x), ddy(pos.y), 0, 0);`) *in addition to* the
   `CFGStmt` for the enclosing full expression/statement. The pre-
   existing `findRequiresUniformCalls`, which recursively walks each
   block's `CFGStmt`s looking for barrier-like calls, had no way to know
   these two `CFGStmt`s overlapped, and so visited (and reported) the
   same `CallExpr` twice for any "requires-uniform-control-flow" call
   that appears as a sub-expression rather than a standalone statement --
   which barriers always are in practice, but `ddx`/`ddy` commonly are
   not. This was caught by noticing the new quad-uniformity test's
   manual `dxc` invocation printed each `ddx`/`ddy` warning twice; fixed
   by deduplicating on `CallExpr` pointer identity via a
   `SmallPtrSet<const CallExpr *, 8>` shared across all of a block's
   `CFGStmt`s.

### Testing

- `tools/clang/test/SemaHLSL/nonuniform-quad-control-flow.hlsl` is a new
  `-verify` test mirroring the structure of the existing
  `nonuniform-control-flow.hlsl`, covering: true positives for `ddx`/
  `ddy` guarded by an `SV_Position`-based branch; true negatives for a
  cbuffer-uniform branch, no branch at all, a `WaveActiveAllTrue` guard
  (group-uniform implies quad-uniform), and `Quad*` broadcasts
  (`QuadReadAcrossX` stored to a local, and `QuadAny` used directly) used
  to guard a quad-scope operation; and a true positive confirming a
  `Quad*` broadcast is *not* sufficient to guard a group-scope barrier,
  which specifically exercises that the two scopes are tracked
  independently rather than being conflated.
- The full `SemaHLSL` lit suite (284 tests, now including both
  uniformity test files) was re-run via `llvm-lit` and passes with zero
  regressions.
- Manually re-checked, via direct `bin/dxc` invocations (since, as noted
  above, plain-CLI warning output for this build/environment requires
  care to observe), that: `WaveReadLaneFirst`/`QuadReadAcrossX` results
  stored into a local variable and then branched on no longer produce
  false-positive warnings (bug #1 above); a `ddx`+`ddy` pair inside a
  single compound-expression statement produces exactly one warning each,
  not two (bug #2 above); and that a handful of pre-existing
  `HLSLFileCheck` samples exercising `ddx`/`ddy` inside real,
  non-synthetic pixel-shader control flow (`POM_PS.hlsl`,
  `RenderVarianceScenePS.hlsl`, the SM6.6 compute/mesh/amplification
  derivatives test) still compile successfully and produce only their
  pre-existing, unrelated warnings (implicit vector truncation), not any
  new spurious quad-uniformity warnings.


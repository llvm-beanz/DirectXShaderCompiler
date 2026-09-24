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

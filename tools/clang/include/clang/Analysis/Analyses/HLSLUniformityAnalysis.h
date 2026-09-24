//= HLSLUniformityAnalysis.h - Find uses of control flow requiring uniformity
//===----------------------------------------------------------------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file defines APIs for invoking and reporting diagnostics for the HLSL
// control-flow uniformity analysis. This analysis is structured like Clang's
// uninitialized value analysis (see UninitializedValues.h): it walks the CFG
// of a function body computing, at every program point, whether values may
// carry information that differs between the invocations (threads/lanes) of
// a thread group ("non-uniform" values), and whether control flow itself may
// therefore diverge between invocations. It then reports any call to an
// operation that requires uniform (non-divergent) control flow -- such as
// GroupMemoryBarrierWithGroupSync -- that provably occurs within a branch
// whose condition may be non-uniform.
//
// The analysis distinguishes two scopes of required uniformity:
//
//   - Group uniformity: the operation (e.g. a GroupSync barrier) requires
//     that every invocation of the entire thread group/wave execute it
//     together.
//   - Quad uniformity: the operation (e.g. ddx/ddy or an explicit Quad*
//     intrinsic) only requires that the four invocations making up a single
//     2x2 "quad" execute it together; it is fine for the decision to differ
//     between different quads.
//
// A value that is uniform across the whole group is trivially uniform
// within any single quad, but the converse is not true, so the analysis
// tracks both properties.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLANG_ANALYSIS_ANALYSES_HLSLUNIFORMITYANALYSIS_H
#define LLVM_CLANG_ANALYSIS_ANALYSES_HLSLUNIFORMITYANALYSIS_H

namespace clang {

class AnalysisDeclContext;
class CallExpr;
class DeclContext;
class Expr;

/// The scope of invocations across which an operation requires its callers
/// to have uniform (non-divergent) control flow.
enum class HLSLUniformityRequirement {
  /// The operation requires every invocation of the thread group/wave to
  /// execute it together (e.g. GroupMemoryBarrierWithGroupSync).
  Group,
  /// The operation requires every invocation of a single 2x2 quad to
  /// execute it together (e.g. ddx/ddy, QuadReadAcrossX).
  Quad,
};

/// Handler for diagnostics produced by the HLSL uniformity analysis.
class HLSLUniformityHandler {
public:
  HLSLUniformityHandler() {}
  virtual ~HLSLUniformityHandler();

  /// Called when \p Call is a use of an operation that requires uniform
  /// control flow (at the scope described by \p Requirement), but the
  /// analysis has determined that the call provably occurs within a branch
  /// whose controlling condition, \p Condition, may be non-uniform at that
  /// scope (i.e. may evaluate differently across the invocations to which
  /// uniformity is required).
  virtual void
  handleNonUniformControlFlowUse(const CallExpr *Call, const Expr *Condition,
                                  HLSLUniformityRequirement Requirement) {}
};

/// Runs the HLSL control-flow uniformity analysis over the body represented
/// by \p cfg (built from \p dc using \p ac), reporting diagnostics to
/// \p handler.
void runHLSLUniformityAnalysis(const DeclContext &dc, AnalysisDeclContext &ac,
                                HLSLUniformityHandler &handler);

} // end namespace clang

#endif

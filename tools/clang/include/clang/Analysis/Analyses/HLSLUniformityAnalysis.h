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
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLANG_ANALYSIS_ANALYSES_HLSLUNIFORMITYANALYSIS_H
#define LLVM_CLANG_ANALYSIS_ANALYSES_HLSLUNIFORMITYANALYSIS_H

namespace clang {

class AnalysisDeclContext;
class CallExpr;
class DeclContext;
class Expr;

/// Handler for diagnostics produced by the HLSL uniformity analysis.
class HLSLUniformityHandler {
public:
  HLSLUniformityHandler() {}
  virtual ~HLSLUniformityHandler();

  /// Called when \p Call is a use of an operation that requires uniform
  /// control flow across the thread group, but the analysis has determined
  /// that the call provably occurs within a branch whose controlling
  /// condition, \p Condition, may be non-uniform (i.e. may evaluate
  /// differently across the invocations of the thread group).
  virtual void handleNonUniformControlFlowUse(const CallExpr *Call,
                                               const Expr *Condition) {}
};

/// Runs the HLSL control-flow uniformity analysis over the body represented
/// by \p cfg (built from \p dc using \p ac), reporting diagnostics to
/// \p handler.
void runHLSLUniformityAnalysis(const DeclContext &dc, AnalysisDeclContext &ac,
                                HLSLUniformityHandler &handler);

} // end namespace clang

#endif

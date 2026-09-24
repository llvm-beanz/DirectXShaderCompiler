//= HLSLUniformityAnalysis.cpp - Find uses of control flow requiring uniformity
//===----------------------------------------------------------------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file implements the HLSL control-flow uniformity analysis for
// source-level CFGs. The overall shape of the analysis intentionally mirrors
// UninitializedValues.cpp: a forward "may" dataflow analysis is run over the
// function's CFG, tracking, for each local variable, whether its value may be
// "non-uniform" -- i.e. may differ between the invocations (threads/lanes)
// that execute the shader in lockstep as a group.
//
// A value is a non-uniform source if it is derived from a per-invocation
// input, such as a parameter bound to a system-value semantic like
// SV_DispatchThreadID, or the result of WaveGetLaneIndex(). Non-uniformity
// propagates through most expressions and assignments. Certain HLSL wave
// intrinsics ("active"/reduction/broadcast operations, e.g. WaveActiveSum)
// always produce a value that is uniform across the wave by definition, and
// are treated specially. Others (e.g. WaveGetLaneIndex, WaveIsFirstLane,
// WavePrefixSum) always produce a non-uniform value, regardless of their
// operands, because their result is defined in terms of the calling lane.
//
// Once uniformity of every branch condition in the CFG is known, the
// analysis computes, for every basic block, whether it is provably reachable
// only through a branch controlled by a non-uniform condition (using
// dominance and post-dominance over the CFG to approximate the structured
// "divergent region" of each such branch). Finally, calls to operations that
// require uniform control flow across the thread group (such as
// GroupMemoryBarrierWithGroupSync) that occur in such a block are reported.
//
//===----------------------------------------------------------------------===//

#include "clang/Analysis/Analyses/HLSLUniformityAnalysis.h"
#include "clang/AST/ASTContext.h"
#include "clang/AST/Attr.h"
#include "clang/AST/Decl.h"
#include "clang/AST/Expr.h"
#include "clang/AST/HlslTypes.h"
#include "clang/Analysis/AnalysisContext.h"
#include "clang/Analysis/Analyses/Dominators.h"
#include "clang/Analysis/CFG.h"
#include "dxc/DXIL/DxilConstants.h"
#include "dxc/HlslIntrinsicOp.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/SmallBitVector.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/Support/GenericDomTree.h"

using namespace clang;
using llvm::SmallBitVector;

//===----------------------------------------------------------------------===//
// Non-uniform semantic recognition.
//===----------------------------------------------------------------------===//

namespace {

// System-value semantics whose value is known to vary between the
// invocations of a thread group (i.e. between threads or wave lanes). This
// list is intentionally conservative: it only includes semantics that are
// unambiguously per-invocation. It is not necessarily exhaustive.
const char *const NonUniformSemantics[] = {
    "sv_dispatchthreadid", "sv_groupthreadid",     "sv_groupindex",
    "sv_vertexid",         "sv_instanceid",        "sv_primitiveid",
    "sv_outputcontrolpointid", "sv_gsinstanceid",  "sv_sampleindex",
    "sv_isfrontface",      "sv_viewid",            "sv_position",
};

bool isNonUniformSemanticName(llvm::StringRef Name) {
  for (const char *S : NonUniformSemantics)
    if (Name.equals_lower(S))
      return true;
  return false;
}

// Returns true if any (possibly nested) field of RD carries a non-uniform
// semantic.
bool recordHasNonUniformSemantic(const RecordDecl *RD,
                                  llvm::SmallPtrSetImpl<const RecordDecl *> &Visited) {
  if (!RD || !Visited.insert(RD).second)
    return false;
  for (const FieldDecl *FD : RD->fields()) {
    for (const hlsl::UnusualAnnotation *UA : FD->getUnusualAnnotations()) {
      if (UA->getKind() == hlsl::UnusualAnnotation::UA_SemanticDecl &&
          isNonUniformSemanticName(
              cast<hlsl::SemanticDecl>(UA)->SemanticName))
        return true;
    }
    if (const auto *RT = FD->getType()->getAs<RecordType>())
      if (recordHasNonUniformSemantic(RT->getDecl(), Visited))
        return true;
  }
  return false;
}

// Returns true if `VD` is a source of non-uniform values: a parameter (or,
// conservatively, a struct-typed parameter with a nested field) bound to a
// system-value semantic known to vary across the invocations of the group.
bool isNonUniformSourceDecl(const VarDecl *VD) {
  for (const hlsl::UnusualAnnotation *UA : VD->getUnusualAnnotations()) {
    if (UA->getKind() == hlsl::UnusualAnnotation::UA_SemanticDecl &&
        isNonUniformSemanticName(cast<hlsl::SemanticDecl>(UA)->SemanticName))
      return true;
  }
  if (const auto *RT = VD->getType()->getAs<RecordType>()) {
    llvm::SmallPtrSet<const RecordDecl *, 8> Visited;
    if (recordHasNonUniformSemantic(RT->getDecl(), Visited))
      return true;
  }
  return false;
}

//===----------------------------------------------------------------------===//
// Intrinsic classification.
//===----------------------------------------------------------------------===//

// Returns the HLSL intrinsic opcode for a direct call to a builtin HLSL
// intrinsic function, or None if `CE` is not such a call.
llvm::Optional<hlsl::IntrinsicOp> getIntrinsicOp(const CallExpr *CE) {
  const FunctionDecl *FD = CE->getDirectCallee();
  if (!FD)
    return None;
  const HLSLIntrinsicAttr *Attr = FD->getAttr<HLSLIntrinsicAttr>();
  if (!Attr || Attr->getGroup() != "op")
    return None;
  return static_cast<hlsl::IntrinsicOp>(Attr->getOpcode());
}

// Wave intrinsics whose result is, by definition, uniform across all active
// lanes of the wave (broadcast/reduction operations), regardless of whether
// their operands are uniform.
bool producesUniformResult(hlsl::IntrinsicOp Op) {
  using hlsl::IntrinsicOp;
  switch (Op) {
  case IntrinsicOp::IOP_WaveActiveAllEqual:
  case IntrinsicOp::IOP_WaveActiveAllTrue:
  case IntrinsicOp::IOP_WaveActiveAnyTrue:
  case IntrinsicOp::IOP_WaveActiveBallot:
  case IntrinsicOp::IOP_WaveActiveBitAnd:
  case IntrinsicOp::IOP_WaveActiveBitOr:
  case IntrinsicOp::IOP_WaveActiveBitXor:
  case IntrinsicOp::IOP_WaveActiveCountBits:
  case IntrinsicOp::IOP_WaveActiveMax:
  case IntrinsicOp::IOP_WaveActiveMin:
  case IntrinsicOp::IOP_WaveActiveProduct:
  case IntrinsicOp::IOP_WaveActiveSum:
  case IntrinsicOp::IOP_WaveActiveUMax:
  case IntrinsicOp::IOP_WaveActiveUMin:
  case IntrinsicOp::IOP_WaveActiveUProduct:
  case IntrinsicOp::IOP_WaveActiveUSum:
  case IntrinsicOp::IOP_WaveGetLaneCount:
  case IntrinsicOp::IOP_WaveReadLaneFirst:
    return true;
  default:
    return false;
  }
}

// Wave intrinsics whose result is always non-uniform, because it is defined
// in terms of the calling lane's position within the wave, regardless of
// whether their operands (if any) are uniform.
bool alwaysProducesNonUniformResult(hlsl::IntrinsicOp Op) {
  using hlsl::IntrinsicOp;
  switch (Op) {
  case IntrinsicOp::IOP_WaveGetLaneIndex:
  case IntrinsicOp::IOP_WaveIsFirstLane:
  case IntrinsicOp::IOP_WaveMatch:
  case IntrinsicOp::IOP_WaveMultiPrefixBitAnd:
  case IntrinsicOp::IOP_WaveMultiPrefixBitOr:
  case IntrinsicOp::IOP_WaveMultiPrefixBitXor:
  case IntrinsicOp::IOP_WaveMultiPrefixCountBits:
  case IntrinsicOp::IOP_WaveMultiPrefixProduct:
  case IntrinsicOp::IOP_WaveMultiPrefixSum:
  case IntrinsicOp::IOP_WaveMultiPrefixUProduct:
  case IntrinsicOp::IOP_WaveMultiPrefixUSum:
  case IntrinsicOp::IOP_WavePrefixCountBits:
  case IntrinsicOp::IOP_WavePrefixProduct:
  case IntrinsicOp::IOP_WavePrefixSum:
  case IntrinsicOp::IOP_WavePrefixUProduct:
  case IntrinsicOp::IOP_WavePrefixUSum:
  case IntrinsicOp::IOP_NonUniformResourceIndex:
    return true;
  default:
    return false;
  }
}

// Returns true if `CE` is a call to an operation that requires uniform
// (non-divergent) control flow across the thread group.
bool requiresUniformControlFlow(const CallExpr *CE, hlsl::IntrinsicOp Op) {
  using hlsl::IntrinsicOp;
  switch (Op) {
  case IntrinsicOp::IOP_GroupMemoryBarrierWithGroupSync:
  case IntrinsicOp::IOP_AllMemoryBarrierWithGroupSync:
  case IntrinsicOp::IOP_DeviceMemoryBarrierWithGroupSync:
    return true;
  case IntrinsicOp::IOP_Barrier: {
    // The generic Barrier() intrinsic only requires uniform control flow
    // when it is asked to synchronize the thread group (GROUP_SYNC). If the
    // semantic flags cannot be proven not to include GROUP_SYNC, be
    // conservative and require uniformity.
    if (CE->getNumArgs() < 2)
      return true;
    const Expr *FlagsExpr = CE->getArg(CE->getNumArgs() - 1);
    llvm::APSInt FlagsVal;
    if (FlagsExpr->isIntegerConstantExpr(FlagsVal, CE->getCalleeDecl()
                                                        ->getASTContext())) {
      uint32_t Flags = FlagsVal.getLimitedValue();
      return (Flags &
              static_cast<uint32_t>(hlsl::DXIL::BarrierSemanticFlag::GroupSync)) != 0;
    }
    return true;
  }
  default:
    return false;
  }
}

//===----------------------------------------------------------------------===//
// DeclToIndex: map tracked local variables/parameters to bit indices.
//===----------------------------------------------------------------------===//

bool isTrackedDecl(const VarDecl *VD, const DeclContext *DC) {
  return VD->getDeclContext() == DC && !VD->hasGlobalStorage() &&
         !VD->isExceptionVariable() && !VD->isInitCapture() &&
         !VD->isImplicit();
}

class DeclToIndex {
  llvm::DenseMap<const VarDecl *, unsigned> Map;

public:
  void computeMap(const DeclContext &DC) {
    unsigned Count = 0;
    for (auto I = DC.decls_begin(), E = DC.decls_end(); I != E; ++I) {
      if (const auto *VD = dyn_cast<VarDecl>(*I))
        if (isTrackedDecl(VD, &DC))
          Map[VD] = Count++;
    }
    // Parameters live in the FunctionDecl itself, not in its DeclContext
    // decls range.
    if (const auto *FD = dyn_cast<FunctionDecl>(&DC)) {
      for (const ParmVarDecl *PD : FD->parameters())
        if (isTrackedDecl(PD, &DC))
          Map[PD] = Count++;
    }
  }

  unsigned size() const { return Map.size(); }

  llvm::Optional<unsigned> getValueIndex(const VarDecl *VD) const {
    auto I = Map.find(VD);
    if (I == Map.end())
      return None;
    return I->second;
  }
};

//===----------------------------------------------------------------------===//
// Expression uniformity evaluation.
//===----------------------------------------------------------------------===//

// Evaluates whether `E` may produce a non-uniform value, given the current
// taint state of tracked local variables in `Env` (bit i set means variable
// with index i may be non-uniform).
bool isExprNonUniform(const Expr *E, const DeclToIndex &DeclIdx,
                      const SmallBitVector &Env) {
  if (!E)
    return false;
  E = E->IgnoreParenCasts();

  if (const auto *DRE = dyn_cast<DeclRefExpr>(E)) {
    if (const auto *VD = dyn_cast<VarDecl>(DRE->getDecl())) {
      if (isNonUniformSourceDecl(VD))
        return true;
      if (llvm::Optional<unsigned> Idx = DeclIdx.getValueIndex(VD))
        return Env.test(*Idx);
    }
    return false;
  }

  if (const auto *ME = dyn_cast<MemberExpr>(E)) {
    if (const auto *FD = dyn_cast<FieldDecl>(ME->getMemberDecl())) {
      for (const hlsl::UnusualAnnotation *UA : FD->getUnusualAnnotations()) {
        if (UA->getKind() == hlsl::UnusualAnnotation::UA_SemanticDecl &&
            isNonUniformSemanticName(
                cast<hlsl::SemanticDecl>(UA)->SemanticName))
          return true;
      }
    }
    return isExprNonUniform(ME->getBase(), DeclIdx, Env);
  }

  if (const auto *CE = dyn_cast<CallExpr>(E)) {
    if (llvm::Optional<hlsl::IntrinsicOp> Op = getIntrinsicOp(CE)) {
      if (alwaysProducesNonUniformResult(*Op))
        return true;
      if (producesUniformResult(*Op))
        return false;
    }
    for (const Expr *Arg : CE->arguments())
      if (isExprNonUniform(Arg, DeclIdx, Env))
        return true;
    return false;
  }

  // Generic fallback: a compound expression is non-uniform if any of its
  // constituent subexpressions are.
  for (const Stmt *Child : E->children()) {
    if (const auto *ChildExpr = dyn_cast_or_null<Expr>(Child))
      if (isExprNonUniform(ChildExpr, DeclIdx, Env))
        return true;
  }
  return false;
}

//===----------------------------------------------------------------------===//
// Dataflow: propagate variable non-uniformity ("taint") through the CFG.
//===----------------------------------------------------------------------===//

// Applies the effect of a single statement to `Env`, tracking assignments to
// local variables.
void transferStmt(const Stmt *S, const DeclToIndex &DeclIdx,
                  SmallBitVector &Env) {
  if (const auto *DS = dyn_cast<DeclStmt>(S)) {
    for (const Decl *D : DS->decls()) {
      const auto *VD = dyn_cast<VarDecl>(D);
      if (!VD || !VD->hasInit())
        continue;
      if (llvm::Optional<unsigned> Idx = DeclIdx.getValueIndex(VD))
        Env.set(*Idx, isExprNonUniform(VD->getInit(), DeclIdx, Env));
    }
    return;
  }

  const Expr *E = dyn_cast<Expr>(S);
  if (!E)
    return;
  E = E->IgnoreParenCasts();

  if (const auto *BO = dyn_cast<BinaryOperator>(E)) {
    if (BO->isAssignmentOp()) {
      bool RHSNonUniform = isExprNonUniform(BO->getRHS(), DeclIdx, Env);
      if (BO->getOpcode() != BO_Assign)
        RHSNonUniform |= isExprNonUniform(BO->getLHS(), DeclIdx, Env);
      if (const auto *DRE =
              dyn_cast<DeclRefExpr>(BO->getLHS()->IgnoreParenCasts())) {
        if (const auto *VD = dyn_cast<VarDecl>(DRE->getDecl()))
          if (llvm::Optional<unsigned> Idx = DeclIdx.getValueIndex(VD))
            Env.set(*Idx, RHSNonUniform);
      }
    }
    return;
  }

  if (const auto *UO = dyn_cast<UnaryOperator>(E)) {
    if (UO->isIncrementDecrementOp()) {
      if (const auto *DRE =
              dyn_cast<DeclRefExpr>(UO->getSubExpr()->IgnoreParenCasts())) {
        if (const auto *VD = dyn_cast<VarDecl>(DRE->getDecl()))
          if (llvm::Optional<unsigned> Idx = DeclIdx.getValueIndex(VD))
            Env.set(*Idx, Env.test(*Idx));
      }
    }
  }
}

//===----------------------------------------------------------------------===//
// Locating calls that require uniform control flow.
//===----------------------------------------------------------------------===//

void findRequiresUniformCalls(const Stmt *S,
                              llvm::SmallVectorImpl<const CallExpr *> &Out) {
  if (!S)
    return;
  if (const auto *CE = dyn_cast<CallExpr>(S)) {
    if (llvm::Optional<hlsl::IntrinsicOp> Op = getIntrinsicOp(CE))
      if (requiresUniformControlFlow(CE, *Op))
        Out.push_back(CE);
  }
  for (const Stmt *Child : S->children())
    findRequiresUniformCalls(Child, Out);
}

} // end anonymous namespace

//===----------------------------------------------------------------------===//
// Driver.
//===----------------------------------------------------------------------===//

HLSLUniformityHandler::~HLSLUniformityHandler() {}

void clang::runHLSLUniformityAnalysis(const DeclContext &dc,
                                      AnalysisDeclContext &ac,
                                      HLSLUniformityHandler &handler) {
  CFG *cfg = ac.getCFG();
  if (!cfg)
    return;

  DeclToIndex DeclIdx;
  DeclIdx.computeMap(dc);
  // Note: even if there are no tracked locals, calls to non-uniform-source
  // intrinsics used directly as branch conditions can still be detected, so
  // we don't bail out here; the (empty) bit vectors below are harmless.

  const unsigned NumVars = DeclIdx.size();

  // Compute the initial ("seed") taint state: parameters that are
  // non-uniform sources start out tainted.
  SmallBitVector Seed(NumVars, false);
  for (auto I = dc.decls_begin(), E = dc.decls_end(); I != E; ++I) {
    if (const auto *VD = dyn_cast<VarDecl>(*I))
      if (isTrackedDecl(VD, &dc) && isNonUniformSourceDecl(VD))
        if (llvm::Optional<unsigned> Idx = DeclIdx.getValueIndex(VD))
          Seed.set(*Idx);
  }
  if (const auto *FD = dyn_cast<FunctionDecl>(&dc)) {
    for (const ParmVarDecl *PD : FD->parameters())
      if (isTrackedDecl(PD, &dc) && isNonUniformSourceDecl(PD))
        if (llvm::Optional<unsigned> Idx = DeclIdx.getValueIndex(PD))
          Seed.set(*Idx);
  }

  const unsigned NumBlocks = cfg->getNumBlockIDs();
  llvm::SmallVector<SmallBitVector, 16> BlockOut(NumBlocks,
                                                 SmallBitVector(NumVars, false));

  // Standard chaotic-iteration forward dataflow to a fixed point. Shader
  // function bodies are small, so a simple worklist-free approach is fine.
  bool Changed = true;
  unsigned IterationGuard = 0;
  while (Changed && IterationGuard++ < NumBlocks * 4 + 16) {
    Changed = false;
    for (CFG::const_iterator BI = cfg->begin(), BE = cfg->end(); BI != BE;
        ++BI) {
      const CFGBlock *Block = *BI;
      SmallBitVector Env(NumVars, false);
      if (Block == &cfg->getEntry()) {
        Env = Seed;
      } else {
        for (CFGBlock::const_pred_iterator PI = Block->pred_begin(),
                                           PE = Block->pred_end();
            PI != PE; ++PI) {
          const CFGBlock *Pred = *PI;
          if (!Pred)
            continue;
          Env |= BlockOut[Pred->getBlockID()];
        }
      }

      for (const CFGElement &Elem : *Block) {
        if (llvm::Optional<CFGStmt> CS = Elem.getAs<CFGStmt>())
          transferStmt(CS->getStmt(), DeclIdx, Env);
      }

      if (Env != BlockOut[Block->getBlockID()]) {
        BlockOut[Block->getBlockID()] = Env;
        Changed = true;
      }
    }
  }

  // Determine which blocks are terminated by a provably non-uniform branch.
  struct DivergentBranch {
    const CFGBlock *Block;
    const Expr *Condition;
  };
  llvm::SmallVector<DivergentBranch, 8> DivergentBranches;
  for (CFG::const_iterator BI = cfg->begin(), BE = cfg->end(); BI != BE;
      ++BI) {
    const CFGBlock *Block = *BI;
    const Stmt *TermCond = Block->getTerminatorCondition();
    const auto *CondExpr = dyn_cast_or_null<Expr>(TermCond);
    if (!CondExpr)
      continue;
    if (isExprNonUniform(CondExpr, DeclIdx, BlockOut[Block->getBlockID()]))
      DivergentBranches.push_back({Block, CondExpr});
  }

  if (DivergentBranches.empty())
    return;

  // Build dominator and post-dominator trees so we can approximate the set
  // of blocks that are only reachable via a divergent branch, i.e. the
  // "divergent region" of that branch, bounded by its reconvergence point
  // (immediate post-dominator).
  DominatorTree DT;
  DT.buildDominatorTree(ac);

  llvm::DominatorTreeBase<CFGBlock> PDT(/*isPostDominator=*/true);
  PDT.recalculate(*cfg);

  for (CFG::const_iterator BI = cfg->begin(), BE = cfg->end(); BI != BE;
      ++BI) {
    const CFGBlock *Block = *BI;
    llvm::SmallVector<const CallExpr *, 4> Calls;
    for (const CFGElement &Elem : *Block) {
      if (llvm::Optional<CFGStmt> CS = Elem.getAs<CFGStmt>())
        findRequiresUniformCalls(CS->getStmt(), Calls);
    }
    if (Calls.empty())
      continue;

    // Find the innermost divergent branch whose region contains this block.
    const DivergentBranch *Innermost = nullptr;
    for (const DivergentBranch &DB : DivergentBranches) {
      if (DB.Block == Block)
        continue;
      if (!DT.properlyDominates(DB.Block, Block))
        continue;
      llvm::DomTreeNodeBase<CFGBlock> *PDN =
          PDT.getNode(const_cast<CFGBlock *>(DB.Block));
      const CFGBlock *IPDom =
          (PDN && PDN->getIDom()) ? PDN->getIDom()->getBlock() : nullptr;
      // Blocks at or beyond the reconvergence point (the branch's immediate
      // post-dominator) are no longer part of the divergent region: they are
      // only reachable once all paths from the branch have merged back
      // together, so exclude any block that is forward-dominated by IPDom.
      if (IPDom && (IPDom == Block || DT.dominates(IPDom, Block)))
        continue;
      if (!Innermost || DT.properlyDominates(Innermost->Block, DB.Block))
        Innermost = &DB;
    }

    if (!Innermost)
      continue;

    for (const CallExpr *Call : Calls)
      handler.handleNonUniformControlFlowUse(Call, Innermost->Condition);
  }
}

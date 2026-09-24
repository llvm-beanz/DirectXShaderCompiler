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
#include "llvm/ADT/SmallPtrSet.h"
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
// their operands are uniform. A group-uniform value is trivially uniform
// within any single quad too, so these ops are uniform at both scopes.
bool producesGroupUniformResult(hlsl::IntrinsicOp Op) {
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

// Quad intrinsics whose result is, by definition, uniform across the four
// invocations making up a single 2x2 quad (broadcast/reduction operations
// scoped to the quad), regardless of whether their operands are uniform.
// Unlike `producesGroupUniformResult`, the result may still differ *between*
// quads (e.g. QuadReadAcrossX broadcasts a value that generally differs from
// quad to quad), so these ops are only uniform at quad scope, not group
// scope.
bool producesQuadUniformResult(hlsl::IntrinsicOp Op) {
  using hlsl::IntrinsicOp;
  switch (Op) {
  case IntrinsicOp::IOP_QuadAll:
  case IntrinsicOp::IOP_QuadAny:
  case IntrinsicOp::IOP_QuadReadAcrossDiagonal:
  case IntrinsicOp::IOP_QuadReadAcrossX:
  case IntrinsicOp::IOP_QuadReadAcrossY:
  case IntrinsicOp::IOP_QuadReadLaneAt:
    return true;
  default:
    return false;
  }
}

// Wave intrinsics whose result is always non-uniform, because it is defined
// in terms of the calling lane's position within the wave, regardless of
// whether their operands (if any) are uniform. This holds at both group and
// quad scope: the four lanes making up a quad generally have distinct wave
// lane positions, so these results generally differ within a quad too.
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

// Returns true if a value produced by `Op` is guaranteed to be uniform at
// the given `Requirement` scope, regardless of its operands' uniformity.
bool producesUniformResult(hlsl::IntrinsicOp Op,
                           HLSLUniformityRequirement Requirement) {
  if (producesGroupUniformResult(Op))
    return true;
  return Requirement == HLSLUniformityRequirement::Quad &&
        producesQuadUniformResult(Op);
}

// Returns the scope of uniform control flow that a call to `Op` (as `CE`)
// requires, or None if it has no such requirement.
llvm::Optional<HLSLUniformityRequirement>
requiresUniformControlFlow(const CallExpr *CE, hlsl::IntrinsicOp Op) {
  using hlsl::IntrinsicOp;
  switch (Op) {
  case IntrinsicOp::IOP_GroupMemoryBarrierWithGroupSync:
  case IntrinsicOp::IOP_AllMemoryBarrierWithGroupSync:
  case IntrinsicOp::IOP_DeviceMemoryBarrierWithGroupSync:
    return HLSLUniformityRequirement::Group;
  case IntrinsicOp::IOP_Barrier: {
    // The generic Barrier() intrinsic only requires uniform control flow
    // when it is asked to synchronize the thread group (GROUP_SYNC). If the
    // semantic flags cannot be proven not to include GROUP_SYNC, be
    // conservative and require uniformity.
    if (CE->getNumArgs() < 2)
      return HLSLUniformityRequirement::Group;
    const Expr *FlagsExpr = CE->getArg(CE->getNumArgs() - 1);
    llvm::APSInt FlagsVal;
    if (FlagsExpr->isIntegerConstantExpr(FlagsVal, CE->getCalleeDecl()
                                                        ->getASTContext())) {
      uint32_t Flags = FlagsVal.getLimitedValue();
      if ((Flags & static_cast<uint32_t>(
                       hlsl::DXIL::BarrierSemanticFlag::GroupSync)) != 0)
        return HLSLUniformityRequirement::Group;
      return None;
    }
    return HLSLUniformityRequirement::Group;
  }
  // Derivative/gradient operations and the explicit Quad* intrinsics only
  // require that the four invocations of a single 2x2 quad execute them
  // together; it is fine for the decision to differ between quads.
  case IntrinsicOp::IOP_ddx:
  case IntrinsicOp::IOP_ddx_coarse:
  case IntrinsicOp::IOP_ddx_fine:
  case IntrinsicOp::IOP_ddy:
  case IntrinsicOp::IOP_ddy_coarse:
  case IntrinsicOp::IOP_ddy_fine:
  case IntrinsicOp::IOP_QuadReadAcrossDiagonal:
  case IntrinsicOp::IOP_QuadReadAcrossX:
  case IntrinsicOp::IOP_QuadReadAcrossY:
  case IntrinsicOp::IOP_QuadReadLaneAt:
  case IntrinsicOp::IOP_QuadAll:
  case IntrinsicOp::IOP_QuadAny:
    return HLSLUniformityRequirement::Quad;
  default:
    return None;
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

// Evaluates whether `E` may produce a value that is non-uniform at the given
// `Requirement` scope, given the current taint state of tracked local
// variables in `Env` (bit i set means variable with index i may be
// non-uniform at that scope).
bool isExprNonUniform(const Expr *E, const DeclToIndex &DeclIdx,
                      const SmallBitVector &Env,
                      HLSLUniformityRequirement Requirement) {
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
    return isExprNonUniform(ME->getBase(), DeclIdx, Env, Requirement);
  }

  if (const auto *CE = dyn_cast<CallExpr>(E)) {
    if (llvm::Optional<hlsl::IntrinsicOp> Op = getIntrinsicOp(CE)) {
      if (alwaysProducesNonUniformResult(*Op))
        return true;
      if (producesUniformResult(*Op, Requirement))
        return false;
    }
    for (const Expr *Arg : CE->arguments())
      if (isExprNonUniform(Arg, DeclIdx, Env, Requirement))
        return true;
    return false;
  }

  // Generic fallback: a compound expression is non-uniform if any of its
  // constituent subexpressions are.
  for (const Stmt *Child : E->children()) {
    if (const auto *ChildExpr = dyn_cast_or_null<Expr>(Child))
      if (isExprNonUniform(ChildExpr, DeclIdx, Env, Requirement))
        return true;
  }
  return false;
}

//===----------------------------------------------------------------------===//
// Dataflow: propagate variable non-uniformity ("taint") through the CFG.
//===----------------------------------------------------------------------===//

// Sets or clears the taint bit for `Idx` in `Env` to `Value`. Note:
// `SmallBitVector` has no `set(unsigned, bool)` overload; naively calling
// `Env.set(Idx, Value)` silently binds to the *range* overload
// `set(unsigned I, unsigned E)` instead (since `bool` converts to
// `unsigned`), which is a completely different, incorrect operation. This
// helper avoids that trap.
void setTaintBit(SmallBitVector &Env, unsigned Idx, bool Value) {
  if (Value)
    Env.set(Idx);
  else
    Env.reset(Idx);
}

// Applies the effect of a single statement to `Env`, tracking assignments to
// local variables, at the given `Requirement` uniformity scope.
void transferStmt(const Stmt *S, const DeclToIndex &DeclIdx,
                  SmallBitVector &Env, HLSLUniformityRequirement Requirement) {
  if (const auto *DS = dyn_cast<DeclStmt>(S)) {
    for (const Decl *D : DS->decls()) {
      const auto *VD = dyn_cast<VarDecl>(D);
      if (!VD || !VD->hasInit())
        continue;
      if (llvm::Optional<unsigned> Idx = DeclIdx.getValueIndex(VD))
        setTaintBit(Env, *Idx,
                   isExprNonUniform(VD->getInit(), DeclIdx, Env, Requirement));
    }
    return;
  }

  const Expr *E = dyn_cast<Expr>(S);
  if (!E)
    return;
  E = E->IgnoreParenCasts();

  if (const auto *BO = dyn_cast<BinaryOperator>(E)) {
    if (BO->isAssignmentOp()) {
      bool RHSNonUniform = isExprNonUniform(BO->getRHS(), DeclIdx, Env, Requirement);
      if (BO->getOpcode() != BO_Assign)
        RHSNonUniform |= isExprNonUniform(BO->getLHS(), DeclIdx, Env, Requirement);
      if (const auto *DRE =
              dyn_cast<DeclRefExpr>(BO->getLHS()->IgnoreParenCasts())) {
        if (const auto *VD = dyn_cast<VarDecl>(DRE->getDecl()))
          if (llvm::Optional<unsigned> Idx = DeclIdx.getValueIndex(VD))
            setTaintBit(Env, *Idx, RHSNonUniform);
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
            setTaintBit(Env, *Idx, Env.test(*Idx));
      }
    }
  }
}


//===----------------------------------------------------------------------===//
// Locating calls that require uniform control flow.
//===----------------------------------------------------------------------===//

struct RequiresUniformCall {
  const CallExpr *Call;
  HLSLUniformityRequirement Requirement;
};

void findRequiresUniformCalls(
    const Stmt *S, llvm::SmallPtrSetImpl<const CallExpr *> &Seen,
    llvm::SmallVectorImpl<RequiresUniformCall> &Out) {
  if (!S)
    return;
  if (const auto *CE = dyn_cast<CallExpr>(S)) {
    if (llvm::Optional<hlsl::IntrinsicOp> Op = getIntrinsicOp(CE))
      if (llvm::Optional<HLSLUniformityRequirement> Requirement =
              requiresUniformControlFlow(CE, *Op))
        if (Seen.insert(CE).second)
          Out.push_back({CE, *Requirement});
  }
  for (const Stmt *Child : S->children())
    findRequiresUniformCalls(Child, Seen, Out);
}

} // end anonymous namespace

//===----------------------------------------------------------------------===//
// Driver.
//===----------------------------------------------------------------------===//

HLSLUniformityHandler::~HLSLUniformityHandler() {}

namespace {

// Holds the results of the forward taint dataflow and the derived set of
// divergent branches for a single uniformity `Requirement` scope (Group or
// Quad).
struct DivergentBranch {
  const CFGBlock *Block;
  const Expr *Condition;
};

struct ScopeAnalysis {
  llvm::SmallVector<SmallBitVector, 16> BlockOut;
  llvm::SmallVector<DivergentBranch, 8> DivergentBranches;
};

// Runs the taint dataflow and divergent-branch identification (steps 1 and 2
// described at the top of this file) for a single uniformity `Requirement`
// scope.
ScopeAnalysis analyzeScope(CFG *cfg, const DeclToIndex &DeclIdx,
                          const SmallBitVector &Seed,
                          HLSLUniformityRequirement Requirement) {
  const unsigned NumVars = DeclIdx.size();
  const unsigned NumBlocks = cfg->getNumBlockIDs();

  ScopeAnalysis Result;
  Result.BlockOut.assign(NumBlocks, SmallBitVector(NumVars, false));

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
          Env |= Result.BlockOut[Pred->getBlockID()];
        }
      }

      for (const CFGElement &Elem : *Block) {
        if (llvm::Optional<CFGStmt> CS = Elem.getAs<CFGStmt>())
          transferStmt(CS->getStmt(), DeclIdx, Env, Requirement);
      }

      if (Env != Result.BlockOut[Block->getBlockID()]) {
        Result.BlockOut[Block->getBlockID()] = Env;
        Changed = true;
      }
    }
  }

  // Determine which blocks are terminated by a provably non-uniform branch.
  for (CFG::const_iterator BI = cfg->begin(), BE = cfg->end(); BI != BE;
      ++BI) {
    const CFGBlock *Block = *BI;
    const Stmt *TermCond = Block->getTerminatorCondition();
    const auto *CondExpr = dyn_cast_or_null<Expr>(TermCond);
    if (!CondExpr)
      continue;
    if (isExprNonUniform(CondExpr, DeclIdx, Result.BlockOut[Block->getBlockID()],
                        Requirement))
      Result.DivergentBranches.push_back({Block, CondExpr});
  }

  return Result;
}

// Finds the innermost of `DivergentBranches` whose divergent region contains
// `Block`, using the (scope-independent) dominator/post-dominator trees, or
// nullptr if none does. See the file header/design notes for how the
// divergent region of a branch is approximated.
const DivergentBranch *
findInnermostDivergentBranch(const CFGBlock *Block, const DominatorTree &DT,
                            llvm::DominatorTreeBase<CFGBlock> &PDT,
                            llvm::ArrayRef<DivergentBranch> DivergentBranches) {
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
  return Innermost;
}

} // end anonymous namespace

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
  // non-uniform sources start out tainted. The same seed is used for both
  // uniformity scopes: these sources are per-invocation values that are not
  // known to be uniform within a quad either.
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

  // Run the taint dataflow and divergent-branch identification once per
  // uniformity scope: group-uniform sync operations (like
  // GroupMemoryBarrierWithGroupSync) need to know about divergence across
  // the whole thread group, while quad-uniform operations (like ddx/ddy)
  // only need to know about divergence within a single quad.
  ScopeAnalysis GroupAnalysis =
      analyzeScope(cfg, DeclIdx, Seed, HLSLUniformityRequirement::Group);
  ScopeAnalysis QuadAnalysis =
      analyzeScope(cfg, DeclIdx, Seed, HLSLUniformityRequirement::Quad);

  if (GroupAnalysis.DivergentBranches.empty() &&
      QuadAnalysis.DivergentBranches.empty())
    return;

  // Build dominator and post-dominator trees so we can approximate the set
  // of blocks that are only reachable via a divergent branch, i.e. the
  // "divergent region" of that branch, bounded by its reconvergence point
  // (immediate post-dominator). These are independent of the uniformity
  // scope, since they only depend on the CFG's structure.
  DominatorTree DT;
  DT.buildDominatorTree(ac);

  llvm::DominatorTreeBase<CFGBlock> PDT(/*isPostDominator=*/true);
  PDT.recalculate(*cfg);

  for (CFG::const_iterator BI = cfg->begin(), BE = cfg->end(); BI != BE;
      ++BI) {
    const CFGBlock *Block = *BI;
    llvm::SmallVector<RequiresUniformCall, 4> Calls;
    llvm::SmallPtrSet<const CallExpr *, 8> SeenCalls;
    for (const CFGElement &Elem : *Block) {
      if (llvm::Optional<CFGStmt> CS = Elem.getAs<CFGStmt>())
        findRequiresUniformCalls(CS->getStmt(), SeenCalls, Calls);
    }
    if (Calls.empty())
      continue;

    const DivergentBranch *InnermostGroup = findInnermostDivergentBranch(
        Block, DT, PDT, GroupAnalysis.DivergentBranches);
    const DivergentBranch *InnermostQuad = findInnermostDivergentBranch(
        Block, DT, PDT, QuadAnalysis.DivergentBranches);

    for (const RequiresUniformCall &RUC : Calls) {
      const DivergentBranch *Innermost =
          RUC.Requirement == HLSLUniformityRequirement::Group
              ? InnermostGroup
              : InnermostQuad;
      if (!Innermost)
        continue;
      handler.handleNonUniformControlFlowUse(RUC.Call, Innermost->Condition,
                                             RUC.Requirement);
    }
  }
}

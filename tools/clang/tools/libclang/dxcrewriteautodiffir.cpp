///////////////////////////////////////////////////////////////////////////////
//                                                                           //
// dxcrewriteautodiffir.cpp                                                  //
// Copyright (C) Microsoft Corporation. All rights reserved.                 //
// This file is distributed under the University of Illinois Open Source     //
// License. See LICENSE.TXT for details.                                     //
//                                                                           //
// Builds the typed, function-local IR used by the auto-diff rewriter.       //
//                                                                           //
///////////////////////////////////////////////////////////////////////////////

#include "dxcrewriteautodiffir.h"

#include "clang/AST/ASTContext.h"
#include "clang/AST/Attr.h"
#include "clang/AST/ExprCXX.h"
#include "clang/AST/HlslTypes.h"
#include "clang/AST/Stmt.h"
#include "llvm/ADT/DenseMap.h"

#include <algorithm>
#include <cassert>

using namespace clang;
using namespace llvm;

namespace hlsl {
namespace autodiff {

ADExpr::ADExpr(Kind K, const Expr *SourceExpr)
    : K(K), Range(SourceExpr ? SourceExpr->getSourceRange() : SourceRange()),
      SourceExpr(SourceExpr) {
  if (!SourceExpr)
    return;
  Value.PrimalType = SourceExpr->getType();
  Value.ValueCategory = SourceExpr->isLValue() ? ADValueCategory::LValue
                                               : ADValueCategory::RValue;
}

namespace {

const ValueDecl *getCanonicalValueDecl(const ValueDecl *D) {
  return D ? cast<ValueDecl>(D->getCanonicalDecl()) : nullptr;
}

ADActivity combineActivity(ArrayRef<const ADExpr *> Operands) {
  for (const ADExpr *Operand : Operands)
    if (Operand->Value.Activity == ADActivity::Active)
      return ADActivity::Active;
  return ADActivity::Inactive;
}

unsigned getComponentCount(QualType Type) {
  if (hlsl::IsHLSLVecType(Type))
    return hlsl::GetHLSLVecSize(Type);
  if (const auto *Vector =
          dyn_cast<VectorType>(Type.getCanonicalType().getTypePtr()))
    return Vector->getNumElements();
  return 1;
}

QualType getElementType(QualType Type) {
  if (hlsl::IsHLSLVecType(Type))
    return hlsl::GetHLSLVecElementType(Type);
  if (const auto *Vector =
          dyn_cast<VectorType>(Type.getCanonicalType().getTypePtr()))
    return Vector->getElementType();
  return Type;
}

bool isDifferentiableFloatingCast(QualType Source, QualType Destination) {
  return getComponentCount(Source) == getComponentCount(Destination) &&
         getElementType(Source)->isFloatingType() &&
         getElementType(Destination)->isFloatingType();
}

class ADFunctionBuilder {
public:
  ADFunctionBuilder(const FunctionDecl *FD, ADFunctionPlan &Plan,
                    std::string &Reason)
      : FD(FD), Ctx(FD->getASTContext()), Plan(Plan), Reason(Reason) {}

  bool build() {
    const auto *Body = dyn_cast_or_null<CompoundStmt>(FD->getBody());
    if (!Body)
      return fail("function has no compound body");

    Plan.Source = FD;
    Plan.ResultType = FD->getReturnType();
    for (const Stmt *S : Body->body())
      if (!buildStmt(S))
        return false;
    return true;
  }

private:
  const FunctionDecl *FD;
  ASTContext &Ctx;
  ADFunctionPlan &Plan;
  std::string &Reason;
  DenseMap<const ValueDecl *, const ADBinding *> CurrentBindings;

  bool fail(StringRef Message) {
    if (Reason.empty())
      Reason = Message.str();
    return false;
  }

  ADExpr *createExpr(ADExpr::Kind K, const Expr *Source) {
    std::unique_ptr<ADExpr> Node(new ADExpr(K, Source));
    ADExpr *Result = Node.get();
    Plan.Expressions.push_back(std::move(Node));
    return Result;
  }

  const ADBinding *createBinding(const VarDecl *VD, unsigned Version,
                                 const ADExpr *Value) {
    std::unique_ptr<ADBinding> Binding(new ADBinding());
    Binding->SourceDecl = VD;
    Binding->Version = Version;
    Binding->Value = Value;
    const ADBinding *Result = Binding.get();
    Plan.Bindings.push_back(std::move(Binding));
    return Result;
  }

  const ADExpr *createLoopUpdateExpr(
      const ADExpr *Expression,
      const DenseMap<const ValueDecl *, unsigned> &StateIndices,
      ArrayRef<unsigned> Versions) {
    if ((Expression->K == ADExpr::Kind::DeclRef ||
         Expression->K == ADExpr::Kind::LocalRef) &&
        Expression->SourceDecl) {
      auto It =
          StateIndices.find(getCanonicalValueDecl(Expression->SourceDecl));
      if (It != StateIndices.end()) {
        ADExpr *StateRef =
            createExpr(ADExpr::Kind::LoopStateRef, Expression->SourceExpr);
        StateRef->Value = Expression->Value;
        StateRef->SourceDecl = Expression->SourceDecl;
        StateRef->LoopStateIndex = It->second;
        StateRef->LoopStateVersion = Versions[It->second];
        return StateRef;
      }
    }

    bool Changed = false;
    SmallVector<const ADExpr *, 4> Operands;
    for (const ADExpr *Operand : Expression->Operands) {
      const ADExpr *Rewritten =
          createLoopUpdateExpr(Operand, StateIndices, Versions);
      Operands.push_back(Rewritten);
      Changed |= Rewritten != Operand;
    }
    const ADExpr *Receiver = Expression->Receiver;
    if (Receiver) {
      const ADExpr *Rewritten =
          createLoopUpdateExpr(Receiver, StateIndices, Versions);
      Changed |= Rewritten != Receiver;
      Receiver = Rewritten;
    }
    if (!Changed)
      return Expression;

    ADExpr *Rewritten = createExpr(Expression->K, Expression->SourceExpr);
    *Rewritten = *Expression;
    Rewritten->Operands = Operands;
    Rewritten->Receiver = Receiver;
    return Rewritten;
  }

  void collectLoopPullbackInputs(
      const ADExpr *Expression,
      SmallVectorImpl<ADLoopPullbackInput> &Inputs) const {
    if (Expression->K == ADExpr::Kind::LoopStateRef) {
      for (const ADLoopPullbackInput &Input : Inputs)
        if (Input.StateIndex == Expression->LoopStateIndex &&
            Input.Version == Expression->LoopStateVersion)
          return;
      Inputs.push_back(
          {Expression->LoopStateIndex, Expression->LoopStateVersion});
      return;
    }
    if (Expression->Receiver)
      collectLoopPullbackInputs(Expression->Receiver, Inputs);
    for (const ADExpr *Operand : Expression->Operands)
      collectLoopPullbackInputs(Operand, Inputs);
  }

  void markLoopPullbackPrimalInputs(
      const ADExpr *Expression,
      SmallVectorImpl<ADLoopPullbackInput> &Inputs) const {
    if (Expression->K == ADExpr::Kind::LoopStateRef) {
      for (ADLoopPullbackInput &Input : Inputs)
        if (Input.StateIndex == Expression->LoopStateIndex &&
            Input.Version == Expression->LoopStateVersion) {
          Input.NeedsPrimal = true;
          return;
        }
      llvm_unreachable("loop pullback primal input was not collected");
    }
    if (Expression->Receiver)
      markLoopPullbackPrimalInputs(Expression->Receiver, Inputs);
    for (const ADExpr *Operand : Expression->Operands)
      markLoopPullbackPrimalInputs(Operand, Inputs);
  }

  void analyzeLoopPullbackPrimalInputs(
      const ADExpr *Expression,
      SmallVectorImpl<ADLoopPullbackInput> &Inputs) const {
    if (Expression->K == ADExpr::Kind::Call && Expression->Callee &&
        Expression->Operands.size() == 1 &&
        StringSwitch<bool>(Expression->Callee->getName())
            .Cases("sin", "cos", "exp", "log", "sqrt", true)
            .Default(false))
      markLoopPullbackPrimalInputs(Expression->Operands.front(), Inputs);
    if (Expression->K == ADExpr::Kind::Binary &&
        Expression->BinaryOpcode == BO_Mul &&
        Expression->Operands[0]->Value.Activity == ADActivity::Active &&
        Expression->Operands[1]->Value.Activity == ADActivity::Active) {
      markLoopPullbackPrimalInputs(Expression->Operands[0], Inputs);
      markLoopPullbackPrimalInputs(Expression->Operands[1], Inputs);
    }
    if (Expression->Receiver)
      analyzeLoopPullbackPrimalInputs(Expression->Receiver, Inputs);
    for (const ADExpr *Operand : Expression->Operands)
      analyzeLoopPullbackPrimalInputs(Operand, Inputs);
  }

  const ADLoopPlan *createLoopPlan(const ForStmt *FS, const VarDecl *Counter,
                                   const ADExpr *TripCount,
                                   ArrayRef<const ADExpr *> Results) {
    std::unique_ptr<ADLoopPlan> Loop(new ADLoopPlan());
    Loop->Source = FS;
    Loop->Counter = Counter;
    Loop->TripCount = TripCount;
    DenseMap<const ValueDecl *, unsigned> StateIndices;
    for (unsigned I = 0; I < Results.size(); ++I) {
      const ADExpr *Result = Results[I];
      ADLoopState State;
      State.SourceDecl = cast<VarDecl>(Result->SourceDecl);
      State.PrimalType = Result->Value.PrimalType;
      State.InitialValue = Result->Operands[0];
      State.Result = Result;
      State.NeedsPrimalTape = Result->RuntimeLoopUsesPrimalTape;
      Loop->States.push_back(State);
      StateIndices[getCanonicalValueDecl(State.SourceDecl)] = I;
      if (Result->RuntimeLoopTapeSize > Loop->TapeCapacity)
        Loop->TapeCapacity = Result->RuntimeLoopTapeSize;
    }

    SmallVector<unsigned, 4> Versions(Results.size(), 0);
    for (unsigned I = 0; I < Results.size(); ++I) {
      const ADExpr *Result = Results[I];
      ADLoopUpdate Update;
      Update.TargetStateIndex = I;
      Update.InputVersion = Versions[I];
      Update.Opcode = Result->BinaryOpcode;
      Update.Value =
          createLoopUpdateExpr(Result->Operands[2], StateIndices, Versions);
      Update.ResultVersion = ++Versions[I];
      Update.Pullback.Value = Update.Value;
      collectLoopPullbackInputs(Update.Value, Update.Pullback.Inputs);
      analyzeLoopPullbackPrimalInputs(Update.Value, Update.Pullback.Inputs);
      for (const ADLoopPullbackInput &Input : Update.Pullback.Inputs) {
        if (!Input.NeedsPrimal)
          continue;
        if (Input.Version == 0) {
          Loop->States[Input.StateIndex].NeedsPrimalTape = true;
          continue;
        }
        bool HasSlot = false;
        for (const ADLoopTapeSlot &Slot : Loop->TapeSlots)
          HasSlot |= Slot.StateIndex == Input.StateIndex &&
                     Slot.Version == Input.Version;
        if (!HasSlot)
          Loop->TapeSlots.push_back({Input.StateIndex, Input.Version});
      }
      Loop->Updates.push_back(Update);
    }
    for (unsigned I = 0; I < Loop->States.size(); ++I)
      Loop->States[I].FinalVersion = Versions[I];
    const ADLoopPlan *Result = Loop.get();
    Plan.Loops.push_back(std::move(Loop));
    return Result;
  }

  const ADExpr *createLocalRef(const Expr *Source, const ADBinding *Binding,
                               bool ForceInactive) {
    ADExpr *Node = createExpr(ADExpr::Kind::LocalRef, Source);
    Node->Binding = Binding;
    Node->SourceDecl = getCanonicalValueDecl(Binding->SourceDecl);
    Node->Value.SourceDecl = Node->SourceDecl;
    Node->Value.Activity =
        ForceInactive ? ADActivity::Inactive : Binding->Value->Value.Activity;
    return Node;
  }

  const ADExpr *createPrimalLocal(const Expr *Source, const VarDecl *VD) {
    ADExpr *Node = createExpr(ADExpr::Kind::PrimalLocal, Source);
    Node->SourceDecl = getCanonicalValueDecl(VD);
    Node->Value.SourceDecl = Node->SourceDecl;
    Node->Value.Activity = ADActivity::Inactive;
    return Node;
  }

  const ADExpr *buildExpr(const Expr *Input, bool ForceInactive = false) {
    if (!Input) {
      fail("null expression");
      return nullptr;
    }

    const Expr *Original = Input;
    if (Ctx.isHLSLNoDiffExpr(Original) ||
        Ctx.isHLSLNoDiffExpr(Original->IgnoreParenImpCasts()))
      ForceInactive = true;

    const Expr *E = Original->IgnoreParenImpCasts();
    if (isa<FloatingLiteral>(E) || isa<IntegerLiteral>(E)) {
      ADExpr *Node = createExpr(ADExpr::Kind::Literal, E);
      Node->Value.Activity = ADActivity::Inactive;
      return Node;
    }

    if (const auto *DRE = dyn_cast<DeclRefExpr>(E)) {
      const ValueDecl *D = getCanonicalValueDecl(DRE->getDecl());
      auto It = CurrentBindings.find(D);
      if (It != CurrentBindings.end()) {
        if (!It->second->Value) {
          if (!ForceInactive) {
            fail("read of uninitialized local in typed auto-diff IR");
            return nullptr;
          }
          return createPrimalLocal(E, cast<VarDecl>(DRE->getDecl()));
        }
        return createLocalRef(E, It->second, ForceInactive);
      }

      ADExpr *Node = createExpr(ADExpr::Kind::DeclRef, E);
      Node->SourceDecl = D;
      Node->Value.SourceDecl = D;
      const auto *Parameter = dyn_cast<ParmVarDecl>(D);
      Node->Value.Activity =
          !ForceInactive && Parameter && !Parameter->hasAttr<HLSLNoDiffAttr>()
              ? ADActivity::Active
              : ADActivity::Inactive;
      return Node;
    }

    if (isa<CXXThisExpr>(E)) {
      ADExpr *Node = createExpr(ADExpr::Kind::This, E);
      Node->Value.Activity = ADActivity::Inactive;
      return Node;
    }

    if (const auto *VE = dyn_cast<HLSLVectorElementExpr>(E)) {
      ADExpr *Node = createExpr(ADExpr::Kind::Swizzle, E);
      const ADExpr *Base = buildExpr(VE->getBase(), ForceInactive);
      if (!Base)
        return nullptr;
      Node->Operands.push_back(Base);
      VE->getEncodedElementAccess(Node->Components);
      Node->Value.Activity =
          ForceInactive ? ADActivity::Inactive : Base->Value.Activity;
      return Node;
    }

    if (const auto *ASE = dyn_cast<ArraySubscriptExpr>(E)) {
      ADExpr *Node = createExpr(ADExpr::Kind::Subscript, E);
      const ADExpr *Base = buildExpr(ASE->getBase(), ForceInactive);
      const ADExpr *Index = buildExpr(ASE->getIdx(), ForceInactive);
      if (!Base || !Index)
        return nullptr;
      if (Index->Value.Activity == ADActivity::Active) {
        fail("active subscript index in typed auto-diff IR");
        return nullptr;
      }
      Node->Operands.push_back(Base);
      Node->Operands.push_back(Index);
      Node->Value.Activity =
          ForceInactive ? ADActivity::Inactive : Base->Value.Activity;
      return Node;
    }

    if (const auto *OCE = dyn_cast<CXXOperatorCallExpr>(E)) {
      if (OCE->getOperator() != OO_Subscript || OCE->getNumArgs() != 2) {
        fail("unsupported operator call in typed auto-diff IR");
        return nullptr;
      }
      ADExpr *Node = createExpr(ADExpr::Kind::Subscript, E);
      const ADExpr *Base = buildExpr(OCE->getArg(0), ForceInactive);
      const ADExpr *Index = buildExpr(OCE->getArg(1), ForceInactive);
      if (!Base || !Index)
        return nullptr;
      if (Index->Value.Activity == ADActivity::Active) {
        fail("active subscript index in typed auto-diff IR");
        return nullptr;
      }
      Node->Operands.push_back(Base);
      Node->Operands.push_back(Index);
      Node->Value.Activity =
          ForceInactive ? ADActivity::Inactive : Base->Value.Activity;
      return Node;
    }

    if (const auto *ME = dyn_cast<MemberExpr>(E)) {
      ADExpr *Node = createExpr(ADExpr::Kind::Member, E);
      Node->SourceDecl = getCanonicalValueDecl(ME->getMemberDecl());
      Node->Value.SourceDecl = Node->SourceDecl;
      const ADExpr *Base = buildExpr(ME->getBase(), ForceInactive);
      if (!Base)
        return nullptr;
      Node->Operands.push_back(Base);
      // Class state is currently treated as a primal constant. Parameter and
      // local activity still flows through explicit expression operands.
      Node->Value.Activity = ADActivity::Inactive;
      return Node;
    }

    if (const auto *ILE = dyn_cast<InitListExpr>(E)) {
      ADExpr *Node = createExpr(ADExpr::Kind::AggregateConstruct, E);
      for (unsigned I = 0; I < ILE->getNumInits(); ++I) {
        const ADExpr *Operand = buildExpr(ILE->getInit(I), ForceInactive);
        if (!Operand)
          return nullptr;
        Node->Operands.push_back(Operand);
      }
      Node->Value.Activity = ForceInactive ? ADActivity::Inactive
                                           : combineActivity(Node->Operands);
      return Node;
    }

    if (const auto *FCE = dyn_cast<CXXFunctionalCastExpr>(E)) {
      const auto *ILE = dyn_cast<InitListExpr>(FCE->getSubExpr());
      if (!ILE) {
        fail("functional cast without initializer list in typed auto-diff IR");
        return nullptr;
      }
      ADExpr *Node = createExpr(ADExpr::Kind::AggregateConstruct, E);
      for (unsigned I = 0; I < ILE->getNumInits(); ++I) {
        const ADExpr *Operand = buildExpr(ILE->getInit(I), ForceInactive);
        if (!Operand)
          return nullptr;
        Node->Operands.push_back(Operand);
      }
      Node->Value.Activity = ForceInactive ? ADActivity::Inactive
                                           : combineActivity(Node->Operands);
      return Node;
    }

    if (const auto *CE = dyn_cast<CastExpr>(E)) {
      ADExpr *Node = createExpr(ADExpr::Kind::Cast, E);
      const ADExpr *Operand = buildExpr(CE->getSubExpr(), ForceInactive);
      if (!Operand)
        return nullptr;
      Node->Operands.push_back(Operand);
      Node->Value.Activity =
          ForceInactive ? ADActivity::Inactive : Operand->Value.Activity;
      if (Node->Value.Activity == ADActivity::Active &&
          !isDifferentiableFloatingCast(Operand->Value.PrimalType,
                                        Node->Value.PrimalType)) {
        fail("active cast not represented in typed auto-diff IR");
        return nullptr;
      }
      return Node;
    }

    if (const auto *UO = dyn_cast<UnaryOperator>(E)) {
      if (UO->getOpcode() != UO_Plus && UO->getOpcode() != UO_Minus) {
        fail("unsupported unary operator in typed auto-diff IR");
        return nullptr;
      }
      ADExpr *Node = createExpr(ADExpr::Kind::Unary, E);
      Node->UnaryOpcode = UO->getOpcode();
      const ADExpr *Operand = buildExpr(UO->getSubExpr(), ForceInactive);
      if (!Operand)
        return nullptr;
      Node->Operands.push_back(Operand);
      Node->Value.Activity =
          ForceInactive ? ADActivity::Inactive : Operand->Value.Activity;
      return Node;
    }

    if (const auto *BO = dyn_cast<BinaryOperator>(E)) {
      bool IsComparison = BO->isComparisonOp();
      bool RequiresInactiveOperands = false;
      switch (BO->getOpcode()) {
      case BO_Add:
      case BO_Sub:
      case BO_Mul:
      case BO_Div:
      case BO_LT:
      case BO_GT:
      case BO_LE:
      case BO_GE:
      case BO_EQ:
      case BO_NE:
        break;
      case BO_Rem:
      case BO_Shl:
      case BO_Shr:
      case BO_And:
      case BO_Xor:
      case BO_Or:
      case BO_LAnd:
      case BO_LOr:
        RequiresInactiveOperands = true;
        break;
      default:
        fail("unsupported binary operator in typed auto-diff IR");
        return nullptr;
      }
      ADExpr *Node = createExpr(ADExpr::Kind::Binary, E);
      Node->BinaryOpcode = BO->getOpcode();
      const ADExpr *Left = buildExpr(BO->getLHS(), ForceInactive);
      const ADExpr *Right = buildExpr(BO->getRHS(), ForceInactive);
      if (!Left || !Right)
        return nullptr;
      Node->Operands.push_back(Left);
      Node->Operands.push_back(Right);
      Node->Value.Activity = ForceInactive ? ADActivity::Inactive
                                           : combineActivity(Node->Operands);
      if (IsComparison && Node->Value.Activity == ADActivity::Active) {
        fail("active comparison in typed auto-diff IR");
        return nullptr;
      }
      if (RequiresInactiveOperands &&
          Node->Value.Activity == ADActivity::Active) {
        fail("active discrete operator in typed auto-diff IR");
        return nullptr;
      }
      return Node;
    }

    if (const auto *CE = dyn_cast<CallExpr>(E)) {
      const FunctionDecl *Callee = CE->getDirectCallee();
      if (!Callee) {
        fail("indirect call in typed auto-diff IR");
        return nullptr;
      }
      ADExpr *Node = createExpr(ADExpr::Kind::Call, E);
      Node->Callee = Callee->getCanonicalDecl();
      if (const auto *MemberCall = dyn_cast<CXXMemberCallExpr>(CE)) {
        Node->Receiver =
            buildExpr(MemberCall->getImplicitObjectArgument(), ForceInactive);
        if (!Node->Receiver)
          return nullptr;
      }
      for (const Expr *Argument : CE->arguments()) {
        const ADExpr *Operand = buildExpr(Argument, ForceInactive);
        if (!Operand)
          return nullptr;
        Node->Operands.push_back(Operand);
      }
      Node->Value.Activity = ForceInactive ? ADActivity::Inactive
                                           : combineActivity(Node->Operands);
      if (!ForceInactive && Node->Receiver &&
          Node->Receiver->Value.Activity == ADActivity::Active)
        Node->Value.Activity = ADActivity::Active;
      return Node;
    }

    fail("unsupported expression in typed auto-diff IR");
    return nullptr;
  }

  bool referencesDecl(const Stmt *S, const ValueDecl *Decl) const {
    if (!S)
      return false;
    if (const auto *DRE = dyn_cast<DeclRefExpr>(S))
      if (getCanonicalValueDecl(DRE->getDecl()) == Decl)
        return true;
    for (const Stmt *Child : S->children())
      if (referencesDecl(Child, Decl))
        return true;
    return false;
  }

  bool containsDeclaration(const Stmt *S) const {
    if (!S)
      return false;
    if (isa<DeclStmt>(S))
      return true;
    for (const Stmt *Child : S->children())
      if (containsDeclaration(Child))
        return true;
    return false;
  }

  bool referencesActiveValue(const Stmt *S) const {
    if (!S)
      return false;
    if (const auto *DRE = dyn_cast<DeclRefExpr>(S)) {
      const ValueDecl *Decl = getCanonicalValueDecl(DRE->getDecl());
      if (const auto *Parameter = dyn_cast<ParmVarDecl>(Decl))
        return !Parameter->hasAttr<HLSLNoDiffAttr>();
      auto It = CurrentBindings.find(Decl);
      return It != CurrentBindings.end() && It->second->Value &&
             It->second->Value->Value.Activity == ADActivity::Active;
    }
    for (const Stmt *Child : S->children())
      if (referencesActiveValue(Child))
        return true;
    return false;
  }

  bool collectInactiveLoopAssignments(
      const Stmt *S, const VarDecl *Counter,
      SmallVectorImpl<std::pair<const ParmVarDecl *, const Expr *>> &Outputs) {
    if (!S)
      return true;
    if (const auto *BO = dyn_cast<BinaryOperator>(S)) {
      if (BO->isAssignmentOp()) {
        const auto *LHS =
            dyn_cast<DeclRefExpr>(BO->getLHS()->IgnoreParenImpCasts());
        if (!LHS)
          return false;
        const auto *Parameter = dyn_cast<ParmVarDecl>(LHS->getDecl());
        if (!Parameter || !Parameter->hasAttr<HLSLNoDiffAttr>())
          return false;
        Outputs.push_back({Parameter, LHS});
      }
    }
    if (const auto *UO = dyn_cast<UnaryOperator>(S)) {
      if (UO->isIncrementDecrementOp()) {
        const auto *Target =
            dyn_cast<DeclRefExpr>(UO->getSubExpr()->IgnoreParenImpCasts());
        if (!Target || Target->getDecl() != Counter)
          return false;
      }
    }
    for (const Stmt *Child : S->children())
      if (!collectInactiveLoopAssignments(Child, Counter, Outputs))
        return false;
    return true;
  }

  bool buildInactiveRuntimeFor(const ForStmt *FS) {
    const auto *Init = dyn_cast_or_null<DeclStmt>(FS->getInit());
    const auto *Counter = Init && Init->isSingleDecl()
                              ? dyn_cast<VarDecl>(Init->getSingleDecl())
                              : nullptr;
    if (!Counter || referencesActiveValue(FS->getCond()) ||
        referencesActiveValue(FS->getBody()))
      return fail("data-dependent control flow (for) is not differentiable");

    SmallVector<std::pair<const ParmVarDecl *, const Expr *>, 4> Outputs;
    if (!collectInactiveLoopAssignments(FS, Counter, Outputs))
      return fail("inactive runtime loop may only update inactive parameters");
    Plan.Statements.push_back({ADStmt::Kind::PrimalLoop, nullptr, nullptr, FS});
    for (const auto &Output : Outputs) {
      const ParmVarDecl *Parameter = Output.first;
      const ValueDecl *Target = getCanonicalValueDecl(Parameter);
      auto It = CurrentBindings.find(Target);
      unsigned Version =
          It == CurrentBindings.end() ? 0 : It->second->Version + 1;
      const ADExpr *Value = createPrimalLocal(Output.second, Parameter);
      CurrentBindings[Target] = createBinding(Parameter, Version, Value);
    }
    return true;
  }

  unsigned getTargetPower(const Expr *Expression,
                          const ParmVarDecl *Target) const {
    Expression = Expression->IgnoreParenImpCasts();
    if (const auto *Ref = dyn_cast<DeclRefExpr>(Expression))
      return Ref->getDecl() == Target ? 1 : 0;
    const auto *Product = dyn_cast<BinaryOperator>(Expression);
    if (!Product || Product->getOpcode() != BO_Mul)
      return 0;
    unsigned Left = getTargetPower(Product->getLHS(), Target);
    unsigned Right = getTargetPower(Product->getRHS(), Target);
    return Left && Right ? Left + Right : 0;
  }

  bool buildIndependentActiveRuntimeFor(const ForStmt *FS,
                                        const VarDecl *Counter,
                                        const ADExpr *Count,
                                        const CompoundStmt *Body) {
    SmallVector<const ADExpr *, 4> Results;
    SmallPtrSet<const ValueDecl *, 4> Targets;
    for (const Stmt *Child : Body->body()) {
      const auto *Update = dyn_cast<BinaryOperator>(Child);
      if (!Update || (Update->getOpcode() != BO_AddAssign &&
                      Update->getOpcode() != BO_SubAssign &&
                      Update->getOpcode() != BO_MulAssign &&
                      Update->getOpcode() != BO_DivAssign))
        return fail("active multi-carried runtime loop requires compound "
                    "updates");
      const auto *TargetRef =
          dyn_cast<DeclRefExpr>(Update->getLHS()->IgnoreParenImpCasts());
      const auto *Target =
          TargetRef ? dyn_cast<ParmVarDecl>(TargetRef->getDecl()) : nullptr;
      if (!Target || Target->hasAttr<HLSLNoDiffAttr>())
        return fail("active multi-carried runtime loop target is not active");
      const ValueDecl *CanonicalTarget = getCanonicalValueDecl(Target);
      if (!Targets.insert(CanonicalTarget).second)
        return fail("active multi-carried runtime loop updates a target twice");

      const ADExpr *Factor = buildExpr(Update->getRHS());
      if (!Factor)
        return false;
      if (Factor->Value.Activity == ADActivity::Active)
        return fail("active multi-carried runtime loop updates must be "
                    "independent");

      auto It = CurrentBindings.find(CanonicalTarget);
      const ADBinding *Before = nullptr;
      if (It == CurrentBindings.end()) {
        ADExpr *Initial = createExpr(ADExpr::Kind::DeclRef, TargetRef);
        Initial->SourceDecl = CanonicalTarget;
        Initial->Value.SourceDecl = CanonicalTarget;
        Initial->Value.Activity = ADActivity::Active;
        Before = createBinding(Target, 0, Initial);
      } else {
        Before = It->second;
      }
      if (!Before->Value)
        return fail(
            "active multi-carried runtime loop reads an uninitialized value");

      ADExpr *Result = createExpr(ADExpr::Kind::RuntimeLoopResult, Update);
      Result->SourceDecl = CanonicalTarget;
      Result->LoopCounter = Counter;
      switch (Update->getOpcode()) {
      case BO_AddAssign:
        Result->BinaryOpcode = BO_Add;
        break;
      case BO_SubAssign:
        Result->BinaryOpcode = BO_Sub;
        break;
      case BO_MulAssign:
        Result->BinaryOpcode = BO_Mul;
        break;
      case BO_DivAssign:
        Result->BinaryOpcode = BO_Div;
        break;
      default:
        llvm_unreachable("validated independent runtime loop update");
      }
      Result->Operands.push_back(createLocalRef(TargetRef, Before, false));
      Result->Operands.push_back(Count);
      Result->Operands.push_back(Factor);
      Result->Value.Activity = ADActivity::Active;
      Result->Value.PrimalType = Target->getType();

      const ADBinding *After =
          createBinding(Target, Before->Version + 1, Result);
      CurrentBindings[CanonicalTarget] = After;
      Results.push_back(Result);
    }
    ADStmt Statement{ADStmt::Kind::ActiveLoop, nullptr, Results.front(), FS};
    Statement.Values = Results;
    Statement.Loop = createLoopPlan(FS, Counter, Count, Results);
    Plan.Statements.push_back(std::move(Statement));
    return true;
  }

  bool buildGenericVersionedRuntimeFor(const ForStmt *FS,
                                       const VarDecl *Counter,
                                       const ADExpr *Count,
                                       const CompoundStmt *Body) {
    if (Body->size() < 3)
      return false;

    SmallVector<const BinaryOperator *, 4> Updates;
    SmallVector<const ParmVarDecl *, 4> Targets;
    SmallVector<const DeclRefExpr *, 4> TargetRefs;
    SmallPtrSet<const ValueDecl *, 4> SeenTargets;
    for (const Stmt *Child : Body->body()) {
      const auto *Update = dyn_cast<BinaryOperator>(Child);
      if (!Update || (Update->getOpcode() != BO_Assign &&
                      Update->getOpcode() != BO_AddAssign &&
                      Update->getOpcode() != BO_SubAssign &&
                      Update->getOpcode() != BO_MulAssign &&
                      Update->getOpcode() != BO_DivAssign))
        return false;
      const auto *TargetRef =
          dyn_cast<DeclRefExpr>(Update->getLHS()->IgnoreParenImpCasts());
      const auto *Target =
          TargetRef ? dyn_cast<ParmVarDecl>(TargetRef->getDecl()) : nullptr;
      if (!Target || Target->hasAttr<HLSLNoDiffAttr>() ||
          !SeenTargets.insert(getCanonicalValueDecl(Target)).second)
        return false;
      if (!Targets.empty() &&
          !Ctx.hasSameType(Targets.front()->getType(), Target->getType()))
        return false;
      Updates.push_back(Update);
      Targets.push_back(Target);
      TargetRefs.push_back(TargetRef);
    }

    bool UsesUpdatedState = false;
    for (unsigned I = 0; I < Updates.size(); ++I)
      for (unsigned Previous = 0; Previous < I; ++Previous)
        UsesUpdatedState |= referencesDecl(
            Updates[I]->getRHS(), getCanonicalValueDecl(Targets[Previous]));
    if (!UsesUpdatedState)
      return false;

    unsigned TapeSize = 0;
    const auto *Condition = dyn_cast<BinaryOperator>(FS->getCond());
    const auto *BoundCall =
        Condition
            ? dyn_cast<CallExpr>(Condition->getRHS()->IgnoreParenImpCasts())
            : nullptr;
    if (BoundCall && BoundCall->getDirectCallee() &&
        BoundCall->getDirectCallee()->getName() == "min")
      for (const Expr *Argument : BoundCall->arguments())
        if (const auto *Limit =
                dyn_cast<IntegerLiteral>(Argument->IgnoreParenImpCasts()))
          TapeSize = Limit->getValue().getLimitedValue(1025);
    if (TapeSize == 0 || TapeSize > 1024)
      return fail("active generic runtime loop requires a min(count, N) bound "
                  "with N between 1 and 1024");

    SmallVector<const ADBinding *, 4> Before;
    for (unsigned I = 0; I < Targets.size(); ++I) {
      const ValueDecl *CanonicalTarget = getCanonicalValueDecl(Targets[I]);
      auto BindingIt = CurrentBindings.find(CanonicalTarget);
      if (BindingIt != CurrentBindings.end()) {
        if (!BindingIt->second->Value)
          return fail("generic runtime loop reads an uninitialized value");
        Before.push_back(BindingIt->second);
        continue;
      }
      ADExpr *Initial = createExpr(ADExpr::Kind::DeclRef, TargetRefs[I]);
      Initial->SourceDecl = CanonicalTarget;
      Initial->Value.SourceDecl = CanonicalTarget;
      Initial->Value.Activity = ADActivity::Active;
      Before.push_back(createBinding(Targets[I], 0, Initial));
    }

    SmallVector<const ADExpr *, 4> Factors;
    for (const BinaryOperator *Update : Updates) {
      const ADExpr *Factor = buildExpr(Update->getRHS());
      if (!Factor)
        return false;
      Factors.push_back(Factor);
    }

    auto SupportsUpdateExpr = [&](const auto &Self,
                                  const ADExpr *Expression) -> bool {
      if (Expression->Value.Activity == ADActivity::Inactive)
        return true;
      switch (Expression->K) {
      case ADExpr::Kind::DeclRef:
      case ADExpr::Kind::LocalRef:
        return Expression->SourceDecl &&
               SeenTargets.count(getCanonicalValueDecl(Expression->SourceDecl));
      case ADExpr::Kind::Unary:
      case ADExpr::Kind::Cast:
        return Self(Self, Expression->Operands.front());
      case ADExpr::Kind::Call:
        return !Expression->Receiver && Expression->Callee &&
               Expression->Operands.size() == 1 &&
               StringSwitch<bool>(Expression->Callee->getName())
                   .Cases("sin", "cos", "exp", "log", "sqrt", true)
                   .Default(false) &&
               Self(Self, Expression->Operands.front());
      case ADExpr::Kind::Binary:
        switch (Expression->BinaryOpcode) {
        case BO_Add:
        case BO_Sub:
        case BO_Mul:
          return Self(Self, Expression->Operands[0]) &&
                 Self(Self, Expression->Operands[1]);
        case BO_Div:
          return Expression->Operands[1]->Value.Activity ==
                     ADActivity::Inactive &&
                 Self(Self, Expression->Operands[0]);
        default:
          return false;
        }
      default:
        return false;
      }
    };
    auto IsStateValue = [&](const ADExpr *Expression) {
      return Expression->Value.Activity == ADActivity::Active &&
             (Expression->K == ADExpr::Kind::DeclRef ||
              Expression->K == ADExpr::Kind::LocalRef) &&
             Expression->SourceDecl &&
             SeenTargets.count(getCanonicalValueDecl(Expression->SourceDecl));
    };
    auto SupportsProductOperand = [&](const auto &Self,
                                      const ADExpr *Expression) -> bool {
      if (IsStateValue(Expression))
        return true;
      switch (Expression->K) {
      case ADExpr::Kind::Unary:
      case ADExpr::Kind::Cast:
        return Self(Self, Expression->Operands.front());
      case ADExpr::Kind::Binary:
        return (Expression->BinaryOpcode == BO_Add ||
                Expression->BinaryOpcode == BO_Sub) &&
               SupportsUpdateExpr(SupportsUpdateExpr,
                                  Expression->Operands[0]) &&
               SupportsUpdateExpr(SupportsUpdateExpr, Expression->Operands[1]);
      default:
        return false;
      }
    };
    for (unsigned I = 0; I < Updates.size(); ++I) {
      if (!SupportsUpdateExpr(SupportsUpdateExpr, Factors[I]))
        return false;
      const ADExpr *Factor = Factors[I];
      switch (Updates[I]->getOpcode()) {
      case BO_Assign:
        if (Factor->K == ADExpr::Kind::Binary &&
            (Factor->BinaryOpcode == BO_Add ||
             Factor->BinaryOpcode == BO_Sub) &&
            Factor->Operands[1]->Value.Activity == ADActivity::Inactive)
          Factor = Factor->Operands[0];
        if (Factor->K == ADExpr::Kind::Call)
          break;
        if (Factor->K != ADExpr::Kind::Binary ||
            Factor->BinaryOpcode != BO_Mul ||
            Factor->Operands[0]->Value.Activity != ADActivity::Active ||
            Factor->Operands[1]->Value.Activity != ADActivity::Active ||
            !SupportsProductOperand(SupportsProductOperand,
                                    Factor->Operands[0]) ||
            !SupportsProductOperand(SupportsProductOperand,
                                    Factor->Operands[1]))
          return false;
        break;
      case BO_AddAssign:
      case BO_SubAssign:
        if (!IsStateValue(Factor))
          return false;
        break;
      case BO_MulAssign:
      case BO_DivAssign:
        if (Factor->Value.Activity == ADActivity::Active)
          return false;
        break;
      default:
        return false;
      }
    }

    SmallVector<const ADExpr *, 4> Results;
    for (unsigned I = 0; I < Targets.size(); ++I) {
      ADExpr *Result = createExpr(ADExpr::Kind::RuntimeLoopResult, Updates[I]);
      Result->SourceDecl = getCanonicalValueDecl(Targets[I]);
      Result->LoopCounter = Counter;
      switch (Updates[I]->getOpcode()) {
      case BO_Assign:
        Result->BinaryOpcode = BO_Assign;
        break;
      case BO_AddAssign:
        Result->BinaryOpcode = BO_Add;
        break;
      case BO_SubAssign:
        Result->BinaryOpcode = BO_Sub;
        break;
      case BO_MulAssign:
        Result->BinaryOpcode = BO_Mul;
        break;
      case BO_DivAssign:
        Result->BinaryOpcode = BO_Div;
        break;
      default:
        llvm_unreachable("validated generic runtime update");
      }
      Result->Operands.push_back(
          createLocalRef(TargetRefs[I], Before[I], false));
      Result->Operands.push_back(Count);
      Result->Operands.push_back(Factors[I]);
      Result->Value.Activity = ADActivity::Active;
      Result->Value.PrimalType = Targets[I]->getType();
      Result->RuntimeLoopUsesPrimalTape = true;
      Result->RuntimeLoopTapeSize = TapeSize;
      Results.push_back(Result);
    }
    for (unsigned I = 0; I < Results.size(); ++I) {
      const ADBinding *After =
          createBinding(Targets[I], Before[I]->Version + 1, Results[I]);
      CurrentBindings[getCanonicalValueDecl(Targets[I])] = After;
    }
    ADStmt Statement(ADStmt::Kind::ActiveLoop, nullptr, Results.front(), FS);
    Statement.Values = Results;
    Statement.Loop = createLoopPlan(FS, Counter, Count, Results);
    Plan.Statements.push_back(std::move(Statement));
    return true;
  }

  bool buildActiveRuntimeChain(const ForStmt *FS, const VarDecl *Counter,
                               const ADExpr *Count, const CompoundStmt *Body) {
    if (Body->size() < 3)
      return false;
    SmallVector<const BinaryOperator *, 4> Updates;
    SmallVector<const ParmVarDecl *, 4> Targets;
    SmallVector<const DeclRefExpr *, 4> TargetRefs;
    SmallPtrSet<const ValueDecl *, 4> SeenTargets;
    for (const Stmt *Child : Body->body()) {
      const auto *Update = dyn_cast<BinaryOperator>(Child);
      if (!Update || (Update->getOpcode() != BO_Assign &&
                      Update->getOpcode() != BO_AddAssign &&
                      Update->getOpcode() != BO_SubAssign &&
                      Update->getOpcode() != BO_MulAssign &&
                      Update->getOpcode() != BO_DivAssign))
        return false;
      const auto *TargetRef =
          dyn_cast<DeclRefExpr>(Update->getLHS()->IgnoreParenImpCasts());
      const auto *Target =
          TargetRef ? dyn_cast<ParmVarDecl>(TargetRef->getDecl()) : nullptr;
      if (!Target || Target->hasAttr<HLSLNoDiffAttr>() ||
          !SeenTargets.insert(getCanonicalValueDecl(Target)).second)
        return false;
      if (!Targets.empty() &&
          !Ctx.hasSameType(Targets.front()->getType(), Target->getType()))
        return false;
      Updates.push_back(Update);
      Targets.push_back(Target);
      TargetRefs.push_back(TargetRef);
    }

    SmallVector<bool, 4> NeedsPrimalTape(Updates.size(), false);
    SmallVector<bool, 4> IsProductUpdate(Updates.size(), false);
    struct ParsedMonomial {
      unsigned TargetPower;
      unsigned PeerPower;
      const Expr *Coefficient;
      bool Subtracts;
      ADExpr::RuntimeLoopMonomial::TargetFunction Function;
    };
    SmallVector<SmallVector<ParsedMonomial, 4>, 4> ProductMonomials(
        Updates.size());
    SmallVector<const Expr *, 4> ProductLinearCoefficients(Updates.size(),
                                                           nullptr);
    SmallVector<bool, 4> ProductSubtractsLinearCoefficient(Updates.size(),
                                                           false);
    SmallVector<const Expr *, 4> ProductPeerLinearCoefficients(Updates.size(),
                                                               nullptr);
    SmallVector<bool, 4> ProductSubtractsPeerLinearCoefficient(Updates.size(),
                                                               false);
    bool NeedsAnyPrimalTape = false;
    for (unsigned I = 0; I + 1 < Updates.size(); ++I) {
      const Expr *FactorExpr = Updates[I]->getRHS()->IgnoreParenImpCasts();
      if (Updates[I]->getOpcode() == BO_Assign) {
        if (const auto *Outer = dyn_cast<BinaryOperator>(FactorExpr)) {
          if ((Outer->getOpcode() == BO_Add || Outer->getOpcode() == BO_Sub) &&
              !referencesActiveValue(Outer->getRHS()))
            FactorExpr = Outer->getLHS()->IgnoreParenImpCasts();
        }
        while (const auto *Polynomial = dyn_cast<BinaryOperator>(FactorExpr)) {
          if (Polynomial->getOpcode() != BO_Add &&
              Polynomial->getOpcode() != BO_Sub)
            break;
          const auto *Linear = dyn_cast<BinaryOperator>(
              Polynomial->getRHS()->IgnoreParenImpCasts());
          if (!Linear || Linear->getOpcode() != BO_Mul)
            break;
          const auto *Left =
              dyn_cast<DeclRefExpr>(Linear->getLHS()->IgnoreParenImpCasts());
          const auto *Right =
              dyn_cast<DeclRefExpr>(Linear->getRHS()->IgnoreParenImpCasts());
          const Expr *Coefficient = nullptr;
          const ValueDecl *LinearTarget = nullptr;
          if (Left && !referencesActiveValue(Linear->getRHS())) {
            LinearTarget = Left->getDecl();
            Coefficient = Linear->getRHS();
          } else if (Right && !referencesActiveValue(Linear->getLHS())) {
            LinearTarget = Right->getDecl();
            Coefficient = Linear->getLHS();
          }
          bool Subtracts = Polynomial->getOpcode() == BO_Sub;
          if (LinearTarget == Targets[I] && !ProductLinearCoefficients[I]) {
            ProductLinearCoefficients[I] = Coefficient;
            ProductSubtractsLinearCoefficient[I] = Subtracts;
          } else if (LinearTarget == Targets[I + 1] &&
                     !ProductPeerLinearCoefficients[I]) {
            ProductPeerLinearCoefficients[I] = Coefficient;
            ProductSubtractsPeerLinearCoefficient[I] = Subtracts;
          } else {
            break;
          }
          FactorExpr = Polynomial->getLHS()->IgnoreParenImpCasts();
        }
        auto MatchMonomial = [&](const Expr *Expression, unsigned &TargetPower,
                                 unsigned &PeerPower, const Expr *&Coefficient,
                                 ADExpr::RuntimeLoopMonomial::TargetFunction
                                     &TargetFunction) {
          auto MatchFactor = [&](const auto &Self, const Expr *Factor) -> bool {
            Factor = Factor->IgnoreParenImpCasts();
            if (const auto *Ref = dyn_cast<DeclRefExpr>(Factor)) {
              if (Ref->getDecl() == Targets[I]) {
                if (TargetFunction !=
                    ADExpr::RuntimeLoopMonomial::TargetFunction::Power)
                  return false;
                ++TargetPower;
                return true;
              }
              if (Ref->getDecl() == Targets[I + 1]) {
                ++PeerPower;
                return true;
              }
            }
            if (const auto *Call = dyn_cast<CallExpr>(Factor)) {
              const FunctionDecl *Callee = Call->getDirectCallee();
              const auto *Argument =
                  Call->getNumArgs() == 1
                      ? dyn_cast<DeclRefExpr>(
                            Call->getArg(0)->IgnoreParenImpCasts())
                      : nullptr;
              StringRef Name = Callee ? Callee->getName() : StringRef();
              if ((Name == "sin" || Name == "cos" || Name == "exp" ||
                   Name == "log" || Name == "sqrt") &&
                  Argument && Argument->getDecl() == Targets[I] &&
                  TargetPower == 0 &&
                  TargetFunction ==
                      ADExpr::RuntimeLoopMonomial::TargetFunction::Power) {
                TargetPower = 1;
                using Function = ADExpr::RuntimeLoopMonomial::TargetFunction;
                TargetFunction = StringSwitch<Function>(Name)
                                     .Case("sin", Function::Sin)
                                     .Case("cos", Function::Cos)
                                     .Case("exp", Function::Exp)
                                     .Case("log", Function::Log)
                                     .Case("sqrt", Function::Sqrt);
                return true;
              }
            }
            if (!referencesActiveValue(Factor)) {
              if (Coefficient)
                return false;
              Coefficient = Factor;
              return true;
            }
            const auto *Product = dyn_cast<BinaryOperator>(Factor);
            return Product && Product->getOpcode() == BO_Mul &&
                   Self(Self, Product->getLHS()) &&
                   Self(Self, Product->getRHS());
          };
          return MatchFactor(MatchFactor, Expression) && TargetPower > 0 &&
                 PeerPower > 0;
        };
        auto CollectMonomials = [&](const auto &Self, const Expr *Expression,
                                    bool Subtracts) -> bool {
          Expression = Expression->IgnoreParenImpCasts();
          if (const auto *Sum = dyn_cast<BinaryOperator>(Expression)) {
            if ((Sum->getOpcode() == BO_Add || Sum->getOpcode() == BO_Sub) &&
                referencesActiveValue(Sum->getRHS()))
              return Self(Self, Sum->getLHS(), Subtracts) &&
                     Self(Self, Sum->getRHS(),
                          Subtracts != (Sum->getOpcode() == BO_Sub));
          }
          unsigned TargetPower = 0;
          unsigned PeerPower = 0;
          const Expr *Coefficient = nullptr;
          auto TargetFunction =
              ADExpr::RuntimeLoopMonomial::TargetFunction::Power;
          if (!MatchMonomial(Expression, TargetPower, PeerPower, Coefficient,
                             TargetFunction))
            return false;
          ProductMonomials[I].push_back(
              {TargetPower, PeerPower, Coefficient, Subtracts, TargetFunction});
          return true;
        };
        if (!CollectMonomials(CollectMonomials, FactorExpr,
                              /*Subtracts=*/false))
          return false;
        IsProductUpdate[I] = true;
      } else {
        if (Updates[I]->getOpcode() != BO_AddAssign &&
            Updates[I]->getOpcode() != BO_SubAssign &&
            Updates[I]->getOpcode() != BO_MulAssign)
          return false;
        const auto *Peer = dyn_cast<DeclRefExpr>(FactorExpr);
        if (!Peer || Peer->getDecl() != Targets[I + 1])
          return false;
        IsProductUpdate[I] = Updates[I]->getOpcode() == BO_MulAssign;
        if (IsProductUpdate[I])
          ProductMonomials[I].push_back(
              {1, 1, nullptr, false,
               ADExpr::RuntimeLoopMonomial::TargetFunction::Power});
      }
      if (IsProductUpdate[I]) {
        NeedsPrimalTape[I] = true;
        NeedsPrimalTape[I + 1] = true;
        NeedsAnyPrimalTape = true;
      }
    }
    const ADExpr *LastFactor = buildExpr(Updates.back()->getRHS());
    if (!LastFactor)
      return false;
    unsigned TapeSize = 0;
    if (LastFactor->Value.Activity == ADActivity::Active) {
      const auto *FactorRef = dyn_cast<DeclRefExpr>(
          Updates.back()->getRHS()->IgnoreParenImpCasts());
      if (Updates.back()->getOpcode() != BO_MulAssign || !FactorRef ||
          FactorRef->getDecl() != Targets.back())
        return false;
      NeedsPrimalTape.back() = true;
      NeedsAnyPrimalTape = true;
    }
    if (NeedsAnyPrimalTape && TapeSize == 0) {
      const auto *Condition = dyn_cast<BinaryOperator>(FS->getCond());
      const auto *BoundCall =
          Condition
              ? dyn_cast<CallExpr>(Condition->getRHS()->IgnoreParenImpCasts())
              : nullptr;
      if (BoundCall && BoundCall->getDirectCallee() &&
          BoundCall->getDirectCallee()->getName() == "min")
        for (const Expr *Argument : BoundCall->arguments())
          if (const auto *Limit =
                  dyn_cast<IntegerLiteral>(Argument->IgnoreParenImpCasts()))
            TapeSize = Limit->getValue().getLimitedValue(1025);
      if (TapeSize == 0 || TapeSize > 1024)
        return fail("active nonlinear runtime chain requires a min(count, N) "
                    "bound with N between 1 and 1024");
    }

    SmallVector<const ADBinding *, 4> Before;
    for (unsigned I = 0; I < Targets.size(); ++I) {
      const ValueDecl *CanonicalTarget = getCanonicalValueDecl(Targets[I]);
      auto BindingIt = CurrentBindings.find(CanonicalTarget);
      if (BindingIt != CurrentBindings.end()) {
        if (!BindingIt->second->Value)
          return fail("linear runtime chain reads an uninitialized value");
        Before.push_back(BindingIt->second);
        continue;
      }
      ADExpr *Initial = createExpr(ADExpr::Kind::DeclRef, TargetRefs[I]);
      Initial->SourceDecl = CanonicalTarget;
      Initial->Value.SourceDecl = CanonicalTarget;
      Initial->Value.Activity = ADActivity::Active;
      Before.push_back(createBinding(Targets[I], 0, Initial));
    }

    SmallVector<const ADExpr *, 4> Results;
    for (unsigned I = 0; I < Targets.size(); ++I) {
      ADExpr *Result = createExpr(ADExpr::Kind::RuntimeLoopResult, Updates[I]);
      Result->SourceDecl = getCanonicalValueDecl(Targets[I]);
      Result->LoopCounter = Counter;
      switch (Updates[I]->getOpcode()) {
      case BO_Assign:
        Result->BinaryOpcode = BO_Assign;
        break;
      case BO_AddAssign:
        Result->BinaryOpcode = BO_Add;
        break;
      case BO_SubAssign:
        Result->BinaryOpcode = BO_Sub;
        break;
      case BO_MulAssign:
        Result->BinaryOpcode = BO_Mul;
        break;
      case BO_DivAssign:
        Result->BinaryOpcode = BO_Div;
        break;
      default:
        llvm_unreachable("validated linear runtime chain update");
      }
      const ADExpr *Factor = nullptr;
      if (I + 1 == Targets.size())
        Factor = LastFactor;
      else if (Updates[I]->getOpcode() == BO_Assign)
        Factor = buildExpr(Updates[I]->getRHS());
      else
        Factor = createLocalRef(Updates[I]->getRHS(), Before[I + 1], false);
      if (!Factor)
        return false;
      Result->Operands.push_back(
          createLocalRef(TargetRefs[I], Before[I], false));
      Result->Operands.push_back(Count);
      Result->Operands.push_back(Factor);
      Result->Value.Activity = ADActivity::Active;
      Result->Value.PrimalType = Targets[I]->getType();
      Result->RuntimeLoopProductUpdate = IsProductUpdate[I];
      for (const ParsedMonomial &Parsed : ProductMonomials[I]) {
        ADExpr::RuntimeLoopMonomial Monomial;
        Monomial.TargetPower = Parsed.TargetPower;
        Monomial.PeerPower = Parsed.PeerPower;
        Monomial.Subtracts = Parsed.Subtracts;
        Monomial.Function = Parsed.Function;
        if (Parsed.Coefficient) {
          Monomial.Coefficient =
              buildExpr(Parsed.Coefficient, /*ForceInactive=*/true);
          if (!Monomial.Coefficient)
            return false;
        }
        Result->RuntimeLoopMonomials.push_back(Monomial);
      }
      if (ProductLinearCoefficients[I]) {
        Result->RuntimeLoopLinearCoefficient =
            buildExpr(ProductLinearCoefficients[I], /*ForceInactive=*/true);
        if (!Result->RuntimeLoopLinearCoefficient)
          return false;
        Result->RuntimeLoopSubtractsLinearCoefficient =
            ProductSubtractsLinearCoefficient[I];
      }
      if (ProductPeerLinearCoefficients[I]) {
        Result->RuntimeLoopPeerLinearCoefficient =
            buildExpr(ProductPeerLinearCoefficients[I], /*ForceInactive=*/true);
        if (!Result->RuntimeLoopPeerLinearCoefficient)
          return false;
        Result->RuntimeLoopSubtractsPeerLinearCoefficient =
            ProductSubtractsPeerLinearCoefficient[I];
      }
      if (NeedsPrimalTape[I]) {
        Result->RuntimeLoopUsesPrimalTape = true;
        Result->RuntimeLoopTapeSize = TapeSize;
      }
      Results.push_back(Result);
    }
    for (unsigned I = 0; I < Results.size(); ++I) {
      ADExpr *Result = const_cast<ADExpr *>(Results[I]);
      Result->RuntimeLoopLinearGroup = Results;
      Result->RuntimeLoopLinearGroupIndex = I;
      const ADBinding *After =
          createBinding(Targets[I], Before[I]->Version + 1, Result);
      CurrentBindings[getCanonicalValueDecl(Targets[I])] = After;
    }
    ADStmt Statement(ADStmt::Kind::ActiveLoop, nullptr, Results.front(), FS);
    Statement.Values = Results;
    Statement.Loop = createLoopPlan(FS, Counter, Count, Results);
    Plan.Statements.push_back(std::move(Statement));
    return true;
  }

  bool buildCoupledActiveRuntimeFor(const ForStmt *FS, const VarDecl *Counter,
                                    const ADExpr *Count,
                                    const CompoundStmt *Body) {
    if (Body->size() != 2)
      return false;
    auto It = Body->body_begin();
    const auto *FirstUpdate = dyn_cast<BinaryOperator>(*It++);
    const auto *SecondUpdate = dyn_cast<BinaryOperator>(*It);
    if (!FirstUpdate || !SecondUpdate ||
        (FirstUpdate->getOpcode() != BO_Assign &&
         FirstUpdate->getOpcode() != BO_AddAssign &&
         FirstUpdate->getOpcode() != BO_SubAssign &&
         FirstUpdate->getOpcode() != BO_MulAssign) ||
        (SecondUpdate->getOpcode() != BO_AddAssign &&
         SecondUpdate->getOpcode() != BO_SubAssign &&
         SecondUpdate->getOpcode() != BO_MulAssign &&
         SecondUpdate->getOpcode() != BO_DivAssign))
      return false;

    const auto *FirstTargetRef =
        dyn_cast<DeclRefExpr>(FirstUpdate->getLHS()->IgnoreParenImpCasts());
    const auto *SecondTargetRef =
        dyn_cast<DeclRefExpr>(SecondUpdate->getLHS()->IgnoreParenImpCasts());
    const auto *FirstTarget =
        FirstTargetRef ? dyn_cast<ParmVarDecl>(FirstTargetRef->getDecl())
                       : nullptr;
    const auto *SecondTarget =
        SecondTargetRef ? dyn_cast<ParmVarDecl>(SecondTargetRef->getDecl())
                        : nullptr;
    if (!FirstTarget || !SecondTarget || FirstTarget == SecondTarget ||
        FirstTarget->hasAttr<HLSLNoDiffAttr>() ||
        SecondTarget->hasAttr<HLSLNoDiffAttr>() ||
        !Ctx.hasSameType(FirstTarget->getType(), SecondTarget->getType()))
      return false;

    const DeclRefExpr *PeerRef = nullptr;
    bool AssignmentProduct = false;
    if (FirstUpdate->getOpcode() == BO_Assign) {
      const Expr *Core = FirstUpdate->getRHS()->IgnoreParenImpCasts();
      if (const auto *Outer = dyn_cast<BinaryOperator>(Core)) {
        if ((Outer->getOpcode() == BO_Add || Outer->getOpcode() == BO_Sub) &&
            !referencesActiveValue(Outer->getRHS()))
          Core = Outer->getLHS()->IgnoreParenImpCasts();
      }
      const auto *Product = dyn_cast<BinaryOperator>(Core);
      if (!Product || Product->getOpcode() != BO_Mul)
        return false;
      const auto *Left =
          dyn_cast<DeclRefExpr>(Product->getLHS()->IgnoreParenImpCasts());
      const auto *Right =
          dyn_cast<DeclRefExpr>(Product->getRHS()->IgnoreParenImpCasts());
      if (Left && Right && Left->getDecl() == FirstTarget &&
          Right->getDecl() == SecondTarget)
        PeerRef = Right;
      else if (Left && Right && Left->getDecl() == SecondTarget &&
               Right->getDecl() == FirstTarget)
        PeerRef = Left;
      else
        return false;
      AssignmentProduct = true;
    } else {
      PeerRef =
          dyn_cast<DeclRefExpr>(FirstUpdate->getRHS()->IgnoreParenImpCasts());
      if (!PeerRef || PeerRef->getDecl() != SecondTarget)
        return false;
    }

    const ADExpr *SecondFactor = buildExpr(SecondUpdate->getRHS());
    if (!SecondFactor || SecondFactor->Value.Activity == ADActivity::Active)
      return false;

    bool UsesPrimalTape =
        FirstUpdate->getOpcode() == BO_MulAssign || AssignmentProduct;
    unsigned TapeSize = 0;
    if (UsesPrimalTape) {
      const auto *Condition = dyn_cast<BinaryOperator>(FS->getCond());
      const auto *BoundCall =
          Condition
              ? dyn_cast<CallExpr>(Condition->getRHS()->IgnoreParenImpCasts())
              : nullptr;
      if (BoundCall && BoundCall->getDirectCallee() &&
          BoundCall->getDirectCallee()->getName() == "min")
        for (const Expr *Argument : BoundCall->arguments())
          if (const auto *Limit =
                  dyn_cast<IntegerLiteral>(Argument->IgnoreParenImpCasts()))
            TapeSize = Limit->getValue().getLimitedValue(1025);
      if (TapeSize == 0 || TapeSize > 1024)
        return fail(
            "active nonlinear coupled runtime loop requires a min(count, N) "
            "bound with N between 1 and 1024");
    }

    auto BuildInitialBinding = [&](const ParmVarDecl *Target,
                                   const DeclRefExpr *TargetRef) {
      const ValueDecl *CanonicalTarget = getCanonicalValueDecl(Target);
      auto BindingIt = CurrentBindings.find(CanonicalTarget);
      if (BindingIt != CurrentBindings.end())
        return BindingIt->second;
      ADExpr *Initial = createExpr(ADExpr::Kind::DeclRef, TargetRef);
      Initial->SourceDecl = CanonicalTarget;
      Initial->Value.SourceDecl = CanonicalTarget;
      Initial->Value.Activity = ADActivity::Active;
      return createBinding(Target, 0, Initial);
    };

    const ADBinding *FirstBefore =
        BuildInitialBinding(FirstTarget, FirstTargetRef);
    const ADBinding *SecondBefore =
        BuildInitialBinding(SecondTarget, SecondTargetRef);
    if (!FirstBefore->Value || !SecondBefore->Value)
      return fail("coupled runtime loop reads an uninitialized value");

    auto CreateResult =
        [&](const ParmVarDecl *Target, const DeclRefExpr *TargetRef,
            const ADBinding *Before, const BinaryOperator *Update,
            const ADExpr *Factor) {
          ADExpr *Result = createExpr(ADExpr::Kind::RuntimeLoopResult, Update);
          Result->SourceDecl = getCanonicalValueDecl(Target);
          Result->LoopCounter = Counter;
          switch (Update->getOpcode()) {
          case BO_Assign:
            Result->BinaryOpcode = BO_Assign;
            break;
          case BO_AddAssign:
            Result->BinaryOpcode = BO_Add;
            break;
          case BO_SubAssign:
            Result->BinaryOpcode = BO_Sub;
            break;
          case BO_MulAssign:
            Result->BinaryOpcode = BO_Mul;
            break;
          case BO_DivAssign:
            Result->BinaryOpcode = BO_Div;
            break;
          default:
            llvm_unreachable("validated coupled runtime loop update");
          }
          Result->Operands.push_back(createLocalRef(TargetRef, Before, false));
          Result->Operands.push_back(Count);
          Result->Operands.push_back(Factor);
          Result->Value.Activity = ADActivity::Active;
          Result->Value.PrimalType = Target->getType();
          return Result;
        };

    const ADExpr *Peer = AssignmentProduct
                             ? buildExpr(FirstUpdate->getRHS())
                             : createLocalRef(PeerRef, SecondBefore, false);
    if (!Peer)
      return false;
    ADExpr *FirstResult = CreateResult(FirstTarget, FirstTargetRef, FirstBefore,
                                       FirstUpdate, Peer);
    ADExpr *SecondResult =
        CreateResult(SecondTarget, SecondTargetRef, SecondBefore, SecondUpdate,
                     SecondFactor);
    FirstResult->RuntimeLoopCoupledPeer = SecondResult;
    FirstResult->RuntimeLoopCoupledPrimary = true;
    FirstResult->RuntimeLoopCoupledUsesPrimalTape = UsesPrimalTape;
    FirstResult->RuntimeLoopSubtractsCoupledPeer =
        FirstUpdate->getOpcode() == BO_SubAssign;
    SecondResult->RuntimeLoopCoupledPeer = FirstResult;
    SecondResult->RuntimeLoopCoupledUsesPrimalTape = UsesPrimalTape;
    if (UsesPrimalTape) {
      FirstResult->RuntimeLoopUsesPrimalTape = true;
      FirstResult->RuntimeLoopTapeSize = TapeSize;
      SecondResult->RuntimeLoopUsesPrimalTape = true;
      SecondResult->RuntimeLoopTapeSize = TapeSize;
    }

    const ADBinding *FirstAfter =
        createBinding(FirstTarget, FirstBefore->Version + 1, FirstResult);
    const ADBinding *SecondAfter =
        createBinding(SecondTarget, SecondBefore->Version + 1, SecondResult);
    CurrentBindings[getCanonicalValueDecl(FirstTarget)] = FirstAfter;
    CurrentBindings[getCanonicalValueDecl(SecondTarget)] = SecondAfter;
    ADStmt Statement(ADStmt::Kind::ActiveLoop, nullptr, FirstResult, FS);
    Statement.Values.push_back(FirstResult);
    Statement.Values.push_back(SecondResult);
    Statement.Loop = createLoopPlan(FS, Counter, Count, Statement.Values);
    Plan.Statements.push_back(std::move(Statement));
    return true;
  }

  bool buildActiveRuntimeFor(const ForStmt *FS) {
    const auto *Init = dyn_cast_or_null<DeclStmt>(FS->getInit());
    const auto *Counter = Init && Init->isSingleDecl()
                              ? dyn_cast<VarDecl>(Init->getSingleDecl())
                              : nullptr;
    const auto *Initial = Counter && Counter->getInit()
                              ? dyn_cast<IntegerLiteral>(
                                    Counter->getInit()->IgnoreParenImpCasts())
                              : nullptr;
    const auto *Condition = dyn_cast_or_null<BinaryOperator>(FS->getCond());
    const auto *ConditionCounter =
        Condition
            ? dyn_cast<DeclRefExpr>(Condition->getLHS()->IgnoreParenImpCasts())
            : nullptr;
    const auto *Increment = dyn_cast_or_null<UnaryOperator>(FS->getInc());
    const auto *IncrementCounter =
        Increment ? dyn_cast<DeclRefExpr>(
                        Increment->getSubExpr()->IgnoreParenImpCasts())
                  : nullptr;
    if (!Counter || !Initial || Initial->getValue() != 0 || !Condition ||
        Condition->getOpcode() != BO_LT || !ConditionCounter ||
        ConditionCounter->getDecl() != Counter || !Increment ||
        (Increment->getOpcode() != UO_PreInc &&
         Increment->getOpcode() != UO_PostInc) ||
        !IncrementCounter || IncrementCounter->getDecl() != Counter)
      return fail("active runtime loop is not canonical");

    const Stmt *Body = FS->getBody();
    if (const auto *Compound = dyn_cast<CompoundStmt>(Body)) {
      if (Compound->size() != 1) {
        const ADExpr *Count = buildExpr(Condition->getRHS());
        if (!Count)
          return false;
        if (Count->Value.Activity == ADActivity::Active)
          return fail("active runtime loop count must be inactive");
        if (buildGenericVersionedRuntimeFor(FS, Counter, Count, Compound))
          return true;
        if (buildActiveRuntimeChain(FS, Counter, Count, Compound))
          return true;
        if (buildCoupledActiveRuntimeFor(FS, Counter, Count, Compound))
          return true;
        return buildIndependentActiveRuntimeFor(FS, Counter, Count, Compound);
      }
      Body = *Compound->body_begin();
    }
    const auto *Update = dyn_cast<BinaryOperator>(Body);
    if (!Update || (Update->getOpcode() != BO_Assign &&
                    Update->getOpcode() != BO_AddAssign &&
                    Update->getOpcode() != BO_SubAssign &&
                    Update->getOpcode() != BO_MulAssign &&
                    Update->getOpcode() != BO_DivAssign))
      return fail("active runtime loop requires =, +=, -=, *=, or /= update");
    const auto *TargetRef =
        dyn_cast<DeclRefExpr>(Update->getLHS()->IgnoreParenImpCasts());
    const auto *Target =
        TargetRef ? dyn_cast<ParmVarDecl>(TargetRef->getDecl()) : nullptr;
    if (!Target || Target->hasAttr<HLSLNoDiffAttr>())
      return fail("active runtime loop target is not an active parameter");

    const ADExpr *Count = buildExpr(Condition->getRHS());
    const ADExpr *Factor = buildExpr(Update->getRHS());
    if (!Count || !Factor)
      return false;
    if (Count->Value.Activity == ADActivity::Active)
      return fail("active runtime loop count must be inactive");

    bool UsesPrimalTape = false;
    unsigned TapeSize = 0;
    const Expr *QuadraticCoefficientExpr = nullptr;
    const Expr *LinearCoefficientExpr = nullptr;
    bool SubtractsLinearCoefficient = false;
    unsigned PolynomialDegree = 2;
    if (Factor->Value.Activity == ADActivity::Active) {
      const auto *FactorRef =
          dyn_cast<DeclRefExpr>(Update->getRHS()->IgnoreParenImpCasts());
      UsesPrimalTape = Update->getOpcode() == BO_MulAssign && FactorRef &&
                       FactorRef->getDecl() == Target;
      auto IsTargetRef = [Target](const Expr *Expression) {
        const auto *Ref =
            dyn_cast<DeclRefExpr>(Expression->IgnoreParenImpCasts());
        return Ref && Ref->getDecl() == Target;
      };
      if (Update->getOpcode() == BO_Assign) {
        const Expr *RHS = Update->getRHS()->IgnoreParenImpCasts();
        const Expr *Core = RHS;
        if (const auto *Outer = dyn_cast<BinaryOperator>(Core)) {
          if ((Outer->getOpcode() == BO_Add || Outer->getOpcode() == BO_Sub) &&
              !referencesActiveValue(Outer->getRHS()))
            Core = Outer->getLHS()->IgnoreParenImpCasts();
        }
        auto MatchPolynomialTerm = [&](const Expr *Expression) {
          unsigned Degree = getTargetPower(Expression, Target);
          if (Degree >= 2) {
            PolynomialDegree = Degree;
            return true;
          }
          const auto *Product =
              dyn_cast<BinaryOperator>(Expression->IgnoreParenImpCasts());
          if (!Product || Product->getOpcode() != BO_Mul)
            return false;
          Degree = getTargetPower(Product->getLHS(), Target);
          if (Degree >= 2 && !referencesActiveValue(Product->getRHS())) {
            QuadraticCoefficientExpr = Product->getRHS();
            PolynomialDegree = Degree;
            return true;
          }
          Degree = getTargetPower(Product->getRHS(), Target);
          if (Degree >= 2 && !referencesActiveValue(Product->getLHS())) {
            QuadraticCoefficientExpr = Product->getLHS();
            PolynomialDegree = Degree;
            return true;
          }
          return false;
        };
        UsesPrimalTape = MatchPolynomialTerm(Core);
        if (const auto *Polynomial = dyn_cast<BinaryOperator>(Core)) {
          if ((Polynomial->getOpcode() == BO_Add ||
               Polynomial->getOpcode() == BO_Sub) &&
              MatchPolynomialTerm(Polynomial->getLHS())) {
            const auto *Linear = dyn_cast<BinaryOperator>(
                Polynomial->getRHS()->IgnoreParenImpCasts());
            if (Linear && Linear->getOpcode() == BO_Mul) {
              if (IsTargetRef(Linear->getLHS()) &&
                  !referencesActiveValue(Linear->getRHS()))
                LinearCoefficientExpr = Linear->getRHS();
              else if (IsTargetRef(Linear->getRHS()) &&
                       !referencesActiveValue(Linear->getLHS()))
                LinearCoefficientExpr = Linear->getLHS();
            }
            SubtractsLinearCoefficient =
                LinearCoefficientExpr && Polynomial->getOpcode() == BO_Sub;
          }
          UsesPrimalTape |= LinearCoefficientExpr != nullptr;
        }
      }
      const auto *BoundCall =
          dyn_cast<CallExpr>(Condition->getRHS()->IgnoreParenImpCasts());
      if (UsesPrimalTape && BoundCall && BoundCall->getDirectCallee() &&
          BoundCall->getDirectCallee()->getName() == "min") {
        for (const Expr *Argument : BoundCall->arguments())
          if (const auto *Limit =
                  dyn_cast<IntegerLiteral>(Argument->IgnoreParenImpCasts()))
            TapeSize = Limit->getValue().getLimitedValue(1025);
      }
      if (!UsesPrimalTape || TapeSize == 0 || TapeSize > 1024)
        return fail("nonlinear active runtime loop requires a min(count, N) "
                    "bound with N between 1 and 1024");
    }

    const ValueDecl *CanonicalTarget = getCanonicalValueDecl(Target);
    auto It = CurrentBindings.find(CanonicalTarget);
    const ADBinding *Before = nullptr;
    if (It == CurrentBindings.end()) {
      ADExpr *Initial = createExpr(ADExpr::Kind::DeclRef, TargetRef);
      Initial->SourceDecl = CanonicalTarget;
      Initial->Value.SourceDecl = CanonicalTarget;
      Initial->Value.Activity = ADActivity::Active;
      Before = createBinding(Target, 0, Initial);
    } else {
      Before = It->second;
    }
    if (!Before->Value)
      return fail("active runtime loop reads an uninitialized value");

    ADExpr *Result = createExpr(ADExpr::Kind::RuntimeLoopResult, Update);
    Result->SourceDecl = CanonicalTarget;
    Result->LoopCounter = Counter;
    Result->RuntimeLoopTapeSize = TapeSize;
    Result->RuntimeLoopUsesPrimalTape = UsesPrimalTape;
    Result->RuntimeLoopPolynomialDegree = PolynomialDegree;
    if (QuadraticCoefficientExpr) {
      Result->RuntimeLoopQuadraticCoefficient =
          buildExpr(QuadraticCoefficientExpr, /*ForceInactive=*/true);
      if (!Result->RuntimeLoopQuadraticCoefficient)
        return false;
    }
    if (LinearCoefficientExpr) {
      Result->RuntimeLoopLinearCoefficient =
          buildExpr(LinearCoefficientExpr, /*ForceInactive=*/true);
      if (!Result->RuntimeLoopLinearCoefficient)
        return false;
    }
    Result->RuntimeLoopSubtractsLinearCoefficient = SubtractsLinearCoefficient;
    switch (Update->getOpcode()) {
    case BO_Assign:
      Result->BinaryOpcode = BO_Assign;
      break;
    case BO_AddAssign:
      Result->BinaryOpcode = BO_Add;
      break;
    case BO_SubAssign:
      Result->BinaryOpcode = BO_Sub;
      break;
    case BO_MulAssign:
      Result->BinaryOpcode = BO_Mul;
      break;
    case BO_DivAssign:
      Result->BinaryOpcode = BO_Div;
      break;
    default:
      llvm_unreachable("validated active runtime loop update");
    }
    Result->Operands.push_back(createLocalRef(TargetRef, Before, false));
    Result->Operands.push_back(Count);
    Result->Operands.push_back(Factor);
    Result->Value.Activity = ADActivity::Active;
    Result->Value.PrimalType = Target->getType();

    const ADBinding *After = createBinding(Target, Before->Version + 1, Result);
    CurrentBindings[CanonicalTarget] = After;
    ADStmt Statement(ADStmt::Kind::ActiveLoop, After, Result, FS);
    Statement.Values.push_back(Result);
    Statement.Loop = createLoopPlan(FS, Counter, Count, Statement.Values);
    Plan.Statements.push_back(std::move(Statement));
    return true;
  }

  bool buildStaticFor(const ForStmt *FS, bool ForceInactive) {
    const auto *Init = dyn_cast_or_null<DeclStmt>(FS->getInit());
    if (!Init || !Init->isSingleDecl())
      return fail("for loop requires one induction variable");
    const auto *Counter = dyn_cast<VarDecl>(Init->getSingleDecl());
    const auto *Initial = Counter && Counter->getInit()
                              ? dyn_cast<IntegerLiteral>(
                                    Counter->getInit()->IgnoreParenImpCasts())
                              : nullptr;
    const auto *Condition = dyn_cast_or_null<BinaryOperator>(FS->getCond());
    const auto *ConditionCounter =
        Condition
            ? dyn_cast<DeclRefExpr>(Condition->getLHS()->IgnoreParenImpCasts())
            : nullptr;
    const auto *Bound = Condition
                            ? dyn_cast<IntegerLiteral>(
                                  Condition->getRHS()->IgnoreParenImpCasts())
                            : nullptr;
    const auto *Increment = dyn_cast_or_null<UnaryOperator>(FS->getInc());
    const auto *IncrementCounter =
        Increment ? dyn_cast<DeclRefExpr>(
                        Increment->getSubExpr()->IgnoreParenImpCasts())
                  : nullptr;
    const ValueDecl *CanonicalCounter = getCanonicalValueDecl(Counter);
    if (!Initial || !Condition || Condition->getOpcode() != BO_LT || !Bound ||
        !Increment ||
        (Increment->getOpcode() != UO_PreInc &&
         Increment->getOpcode() != UO_PostInc) ||
        !ConditionCounter || !IncrementCounter ||
        getCanonicalValueDecl(ConditionCounter->getDecl()) !=
            CanonicalCounter ||
        getCanonicalValueDecl(IncrementCounter->getDecl()) != CanonicalCounter)
      return fail("for loop is not a canonical constant-bound loop");

    uint64_t InitialValue = Initial->getValue().getLimitedValue(65);
    uint64_t BoundValue = Bound->getValue().getLimitedValue(65);
    if (InitialValue > BoundValue || BoundValue - InitialValue > 64)
      return fail("for loop exceeds the static unroll limit");
    if (referencesDecl(FS->getBody(), CanonicalCounter))
      return fail("statically unrolled loop body references its induction "
                  "variable");
    if (containsDeclaration(FS->getBody()))
      return fail("statically unrolled loop body contains a declaration");

    for (uint64_t Iteration = InitialValue; Iteration < BoundValue; ++Iteration)
      if (!buildStmt(FS->getBody(), ForceInactive))
        return false;
    return true;
  }

  bool buildStmt(const Stmt *S, bool ForceInactive = false) {
    if (const auto *AS = dyn_cast<AttributedStmt>(S)) {
      bool IsNoDiff = false;
      for (const Attr *A : AS->getAttrs())
        IsNoDiff |= isa<HLSLNoDiffAttr>(A);
      return buildStmt(AS->getSubStmt(), ForceInactive || IsNoDiff);
    }

    if (const auto *DS = dyn_cast<DeclStmt>(S)) {
      for (const Decl *D : DS->decls()) {
        const auto *VD = dyn_cast<VarDecl>(D);
        if (!VD)
          return fail("unsupported declaration in typed auto-diff IR");
        const ADExpr *Value = nullptr;
        if (VD->hasInit()) {
          Value = buildExpr(VD->getInit(), ForceInactive);
          if (!Value)
            return false;
        }
        const ADBinding *Binding = createBinding(VD, 0, Value);
        CurrentBindings[getCanonicalValueDecl(VD)] = Binding;
        Plan.Statements.push_back({ADStmt::Kind::Declare, Binding, Value});
      }
      return true;
    }

    if (const auto *RS = dyn_cast<ReturnStmt>(S)) {
      const ADExpr *Value = buildExpr(RS->getRetValue(), ForceInactive);
      if (!Value)
        return false;
      Plan.Statements.push_back({ADStmt::Kind::Return, nullptr, Value});
      return true;
    }

    if (const auto *FS = dyn_cast<ForStmt>(S)) {
      const auto *Condition = dyn_cast_or_null<BinaryOperator>(FS->getCond());
      const bool HasConstantBound =
          Condition &&
          isa<IntegerLiteral>(Condition->getRHS()->IgnoreParenImpCasts());
      if (HasConstantBound)
        return buildStaticFor(FS, ForceInactive);
      return referencesActiveValue(FS->getBody()) ? buildActiveRuntimeFor(FS)
                                                  : buildInactiveRuntimeFor(FS);
    }

    if (const auto *IS = dyn_cast<IfStmt>(S)) {
      if (IS->getConditionVariable())
        return fail(
            "if condition variable not represented in typed auto-diff IR");
      const ADExpr *Condition = buildExpr(IS->getCond(), ForceInactive);
      if (!Condition) {
        if (Reason == "active comparison in typed auto-diff IR")
          Reason = "data-dependent control flow (if) is not differentiable; "
                   "use [[dxc::no_diff]] or branchless math";
        return false;
      }
      if (Condition->Value.Activity == ADActivity::Active)
        return fail("active if condition in typed auto-diff IR");

      DenseMap<const ValueDecl *, const ADBinding *> Before = CurrentBindings;
      size_t StatementCount = Plan.Statements.size();

      CurrentBindings = Before;
      if (!buildStmt(IS->getThen(), ForceInactive))
        return false;
      DenseMap<const ValueDecl *, const ADBinding *> Then = CurrentBindings;
      Plan.Statements.resize(StatementCount);

      CurrentBindings = Before;
      if (IS->getElse() && !buildStmt(IS->getElse(), ForceInactive))
        return false;
      DenseMap<const ValueDecl *, const ADBinding *> Else = CurrentBindings;
      Plan.Statements.resize(StatementCount);
      CurrentBindings = Before;

      for (const auto &Entry : Before) {
        const ValueDecl *Decl = Entry.first;
        const ADBinding *OldBinding = Entry.second;
        const ADBinding *ThenBinding = Then.lookup(Decl);
        const ADBinding *ElseBinding = Else.lookup(Decl);
        ThenBinding = ThenBinding ? ThenBinding : OldBinding;
        ElseBinding = ElseBinding ? ElseBinding : OldBinding;
        if (ThenBinding == OldBinding && ElseBinding == OldBinding)
          continue;
        if (!ThenBinding->Value || !ElseBinding->Value)
          return fail("inactive if leaves a local uninitialized");

        ADExpr *Merged = createExpr(ADExpr::Kind::Conditional, IS->getCond());
        Merged->Value.PrimalType = OldBinding->Value->Value.PrimalType;
        Merged->Operands.push_back(Condition);
        Merged->Operands.push_back(ThenBinding->Value);
        Merged->Operands.push_back(ElseBinding->Value);
        Merged->Value.Activity = combineActivity(
            ArrayRef<const ADExpr *>(Merged->Operands).slice(1));

        unsigned Version =
            std::max(ThenBinding->Version, ElseBinding->Version) + 1;
        const ADBinding *Binding =
            createBinding(cast<VarDecl>(Decl), Version, Merged);
        CurrentBindings[Decl] = Binding;
        Plan.Statements.push_back({ADStmt::Kind::Assign, Binding, Merged});
      }
      return true;
    }

    if (const auto *CS = dyn_cast<CompoundStmt>(S)) {
      for (const Stmt *Child : CS->body())
        if (!buildStmt(Child, ForceInactive))
          return false;
      return true;
    }

    if (const auto *Expression = dyn_cast<Expr>(S);
        Expression && !(isa<BinaryOperator>(Expression) &&
                        cast<BinaryOperator>(Expression)->isAssignmentOp())) {
      if (!ForceInactive)
        return fail("active expression statement in typed auto-diff IR");
      const ADExpr *Value = buildExpr(Expression, /*ForceInactive=*/true);
      if (!Value)
        return false;
      const auto *Call = dyn_cast<CallExpr>(Expression->IgnoreParenImpCasts());
      const FunctionDecl *Callee = Call ? Call->getDirectCallee() : nullptr;
      if (Call && Callee) {
        for (unsigned I = 0;
             I < Call->getNumArgs() && I < Callee->getNumParams(); ++I) {
          const auto *Output =
              dyn_cast<DeclRefExpr>(Call->getArg(I)->IgnoreParenImpCasts());
          if (!Output)
            continue;
          const auto *VD = dyn_cast<VarDecl>(Output->getDecl());
          if (!VD)
            continue;
          const ValueDecl *Target = getCanonicalValueDecl(VD);
          auto It = CurrentBindings.find(Target);
          bool IsExplicitOutput =
              Callee->getParamDecl(I)->hasAttr<HLSLOutAttr>() ||
              Callee->getParamDecl(I)->hasAttr<HLSLInOutAttr>();
          bool IsUninitializedLocal =
              It != CurrentBindings.end() && !It->second->Value;
          if (!IsExplicitOutput && !IsUninitializedLocal)
            continue;
          if (It == CurrentBindings.end())
            return fail("inactive out argument has no local binding");
          const ADExpr *OutputValue = createPrimalLocal(Output, VD);
          const ADBinding *Binding =
              createBinding(VD, It->second->Version + 1, OutputValue);
          CurrentBindings[Target] = Binding;
          if (std::find(Plan.PrimalLocals.begin(), Plan.PrimalLocals.end(),
                        VD) == Plan.PrimalLocals.end())
            Plan.PrimalLocals.push_back(VD);
        }
      }
      Plan.Statements.push_back({ADStmt::Kind::Expression, nullptr, Value});
      return true;
    }

    const auto *BO = dyn_cast<BinaryOperator>(S);
    if (!BO || !BO->isAssignmentOp())
      return fail("unsupported statement in typed auto-diff IR");

    const auto *LHS =
        dyn_cast<DeclRefExpr>(BO->getLHS()->IgnoreParenImpCasts());
    if (!LHS)
      return fail("assignment target is not a local variable");
    const ValueDecl *Target = getCanonicalValueDecl(LHS->getDecl());
    auto It = CurrentBindings.find(Target);
    if (It == CurrentBindings.end()) {
      const auto *Parameter = dyn_cast<ParmVarDecl>(LHS->getDecl());
      if (!Parameter)
        return fail("assignment target has no local binding");
      ADExpr *InitialValue = createExpr(ADExpr::Kind::DeclRef, LHS);
      InitialValue->SourceDecl = Target;
      InitialValue->Value.SourceDecl = Target;
      InitialValue->Value.Activity =
          !ForceInactive && !Parameter->hasAttr<HLSLNoDiffAttr>()
              ? ADActivity::Active
              : ADActivity::Inactive;
      CurrentBindings[Target] = createBinding(Parameter, 0, InitialValue);
      It = CurrentBindings.find(Target);
    }

    const ADExpr *Value = buildExpr(BO->getRHS(), ForceInactive);
    if (!Value)
      return false;

    BinaryOperatorKind ValueOpcode = BO_Add;
    bool IsCompound = true;
    bool RequiresInactiveOperands = false;
    switch (BO->getOpcode()) {
    case BO_Assign:
      IsCompound = false;
      break;
    case BO_AddAssign:
      ValueOpcode = BO_Add;
      break;
    case BO_SubAssign:
      ValueOpcode = BO_Sub;
      break;
    case BO_MulAssign:
      ValueOpcode = BO_Mul;
      break;
    case BO_DivAssign:
      ValueOpcode = BO_Div;
      break;
    case BO_RemAssign:
      ValueOpcode = BO_Rem;
      RequiresInactiveOperands = true;
      break;
    case BO_ShlAssign:
      ValueOpcode = BO_Shl;
      RequiresInactiveOperands = true;
      break;
    case BO_ShrAssign:
      ValueOpcode = BO_Shr;
      RequiresInactiveOperands = true;
      break;
    case BO_AndAssign:
      ValueOpcode = BO_And;
      RequiresInactiveOperands = true;
      break;
    case BO_XorAssign:
      ValueOpcode = BO_Xor;
      RequiresInactiveOperands = true;
      break;
    case BO_OrAssign:
      ValueOpcode = BO_Or;
      RequiresInactiveOperands = true;
      break;
    default:
      return fail("unsupported assignment in typed auto-diff IR");
    }

    if (IsCompound) {
      if (!It->second->Value)
        return fail("compound assignment reads an uninitialized local");
      ADExpr *Combined = createExpr(ADExpr::Kind::Binary, BO);
      Combined->BinaryOpcode = ValueOpcode;
      Combined->Operands.push_back(createLocalRef(BO->getLHS(), It->second,
                                                  /*ForceInactive=*/false));
      Combined->Operands.push_back(Value);
      Combined->Value.Activity = combineActivity(Combined->Operands);
      if (RequiresInactiveOperands &&
          Combined->Value.Activity == ADActivity::Active)
        return fail("active discrete assignment in typed auto-diff IR");
      Value = Combined;
    }

    unsigned NewVersion = It->second->Version + 1;
    assert(NewVersion > It->second->Version && "AD binding version overflow");
    const ADBinding *Binding =
        createBinding(cast<VarDecl>(LHS->getDecl()), NewVersion, Value);
    CurrentBindings[Target] = Binding;
    Plan.Statements.push_back({ADStmt::Kind::Assign, Binding, Value});
    return true;
  }
};

} // namespace

bool BuildADFunctionPlan(const FunctionDecl *FD, ADFunctionPlan &Plan,
                         std::string &Reason) {
  Reason.clear();
  ADFunctionBuilder Builder(FD, Plan, Reason);
  return Builder.build();
}

} // namespace autodiff
} // namespace hlsl

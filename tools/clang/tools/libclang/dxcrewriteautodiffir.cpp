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
      if (Compound->size() != 1)
        return fail("active runtime loop requires one update statement");
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
      auto IsSelfProduct = [&IsTargetRef](const Expr *Expression) {
        const auto *Product =
            dyn_cast<BinaryOperator>(Expression->IgnoreParenImpCasts());
        return Product && Product->getOpcode() == BO_Mul &&
               IsTargetRef(Product->getLHS()) && IsTargetRef(Product->getRHS());
      };
      if (Update->getOpcode() == BO_Assign) {
        const Expr *RHS = Update->getRHS()->IgnoreParenImpCasts();
        UsesPrimalTape = IsSelfProduct(RHS);
        if (const auto *Outer = dyn_cast<BinaryOperator>(RHS)) {
          if ((Outer->getOpcode() == BO_Add || Outer->getOpcode() == BO_Sub) &&
              IsSelfProduct(Outer->getLHS()) &&
              !referencesActiveValue(Outer->getRHS()))
            UsesPrimalTape = true;
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
    Plan.Statements.push_back({ADStmt::Kind::ActiveLoop, After, Result, FS});
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

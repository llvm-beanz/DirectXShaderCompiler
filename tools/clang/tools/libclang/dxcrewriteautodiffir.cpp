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
#include "clang/AST/Stmt.h"
#include "llvm/ADT/DenseMap.h"

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
      if (It != CurrentBindings.end())
        return createLocalRef(E, It->second, ForceInactive);

      ADExpr *Node = createExpr(ADExpr::Kind::DeclRef, E);
      Node->SourceDecl = D;
      Node->Value.SourceDecl = D;
      Node->Value.Activity = !ForceInactive && isa<ParmVarDecl>(D)
                                 ? ADActivity::Active
                                 : ADActivity::Inactive;
      return Node;
    }

    if (isa<CXXThisExpr>(E)) {
      ADExpr *Node = createExpr(ADExpr::Kind::This, E);
      Node->Value.Activity = ADActivity::Inactive;
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

    if (const auto *CE = dyn_cast<CastExpr>(E)) {
      if (!ForceInactive) {
        fail("active cast not represented in typed auto-diff IR");
        return nullptr;
      }
      ADExpr *Node = createExpr(ADExpr::Kind::Cast, E);
      const ADExpr *Operand = buildExpr(CE->getSubExpr(), ForceInactive);
      if (!Operand)
        return nullptr;
      Node->Operands.push_back(Operand);
      Node->Value.Activity = ADActivity::Inactive;
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
      switch (BO->getOpcode()) {
      case BO_Add:
      case BO_Sub:
      case BO_Mul:
      case BO_Div:
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
      for (const Expr *Argument : CE->arguments()) {
        const ADExpr *Operand = buildExpr(Argument, ForceInactive);
        if (!Operand)
          return nullptr;
        Node->Operands.push_back(Operand);
      }
      Node->Value.Activity = ForceInactive ? ADActivity::Inactive
                                           : combineActivity(Node->Operands);
      return Node;
    }

    fail("unsupported expression in typed auto-diff IR");
    return nullptr;
  }

  bool buildStmt(const Stmt *S) {
    if (isa<AttributedStmt>(S))
      return fail("attributed statement not represented in typed auto-diff IR");

    if (const auto *DS = dyn_cast<DeclStmt>(S)) {
      for (const Decl *D : DS->decls()) {
        const auto *VD = dyn_cast<VarDecl>(D);
        if (!VD || !VD->hasInit())
          return fail("unsupported declaration in typed auto-diff IR");
        const ADExpr *Value = buildExpr(VD->getInit());
        if (!Value)
          return false;
        const ADBinding *Binding = createBinding(VD, 0, Value);
        CurrentBindings[getCanonicalValueDecl(VD)] = Binding;
        Plan.Statements.push_back({ADStmt::Kind::Declare, Binding, Value});
      }
      return true;
    }

    if (const auto *RS = dyn_cast<ReturnStmt>(S)) {
      const ADExpr *Value = buildExpr(RS->getRetValue());
      if (!Value)
        return false;
      Plan.Statements.push_back({ADStmt::Kind::Return, nullptr, Value});
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
    if (It == CurrentBindings.end())
      return fail("assignment target has no local binding");

    const ADExpr *Value = buildExpr(BO->getRHS());
    if (!Value)
      return false;

    BinaryOperatorKind ValueOpcode = BO_Add;
    bool IsCompound = true;
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
    default:
      return fail("unsupported assignment in typed auto-diff IR");
    }

    if (IsCompound) {
      ADExpr *Combined = createExpr(ADExpr::Kind::Binary, BO);
      Combined->BinaryOpcode = ValueOpcode;
      Combined->Operands.push_back(createLocalRef(BO->getLHS(), It->second,
                                                  /*ForceInactive=*/false));
      Combined->Operands.push_back(Value);
      Combined->Value.Activity = combineActivity(Combined->Operands);
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

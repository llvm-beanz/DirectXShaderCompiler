//===--- HLSLOutParamBuilder.h - Helper for HLSLOutParamExpr ----*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
//
//===----------------------------------------------------------------------===//

#ifndef CLANG_SEMA_HLSLOUTPARAMBUILDER_H
#define CLANG_SEMA_HLSLOUTPARAMBUILDER_H

#include "clang/AST/ASTContext.h"
#include "clang/AST/Expr.h"
#include "clang/AST/StmtVisitor.h"
#include "clang/Sema/Sema.h"
#include "llvm/ADT/DenseSet.h"

namespace clang {

class HLSLOutParamBuilder {
  llvm::DenseSet<VarDecl *> SeenVars;

  // not copyable
  HLSLOutParamBuilder(const HLSLOutParamBuilder &) = delete;
  HLSLOutParamBuilder &operator=(const HLSLOutParamBuilder &) = delete;

  class DeclFinder : public StmtVisitor<DeclFinder> {
  public:
    VarDecl *Decl = nullptr;

    // TODO: For correctness, when multiple decls are found all decls should be
    // added to the Seen list.
    bool MultipleFound = false;

    DeclFinder() = default;

    void VisitStmt(Stmt *S) {
      for (Stmt *Child : S->children())
        if (Child)
          Visit(Child);
    }

    void VisitDeclRefExpr(DeclRefExpr *DRE) {
      if (MultipleFound)
        return;
      if (Decl)
        MultipleFound = true;
      Decl = dyn_cast<VarDecl>(DRE->getFoundDecl());
      return;
    }
  };

public:
  HLSLOutParamBuilder() = default;

  ExprResult Create(Sema &Sema, ParmVarDecl *P, Expr *Base) {
    ASTContext &Ctx = Sema.getASTContext();

    QualType Ty = P->getType().getNonLValueExprType(Ctx);

    if(hlsl::IsHLSLVecMatType(Base->getType()) && Ty->isScalarType()) {
      Sema.Diag(Base->getLocStart(), diag::err_hlsl_unsupported_lvalue_cast_op);
      return ExprError();
    }

    // If the unqualified types mismatch we may have some casting. Since this
    // results in a copy we can ignore qualifiers.
    if (Ty.getUnqualifiedType() != Base->getType().getUnqualifiedType()) {
      ExprResult Res =
          Sema.PerformImplicitConversion(Base, Ty, Sema::AA_Passing);
      if (Res.isInvalid())
        return ExprError();
      HLSLOutParamExpr *OutExpr = HLSLOutParamExpr::Create(
          Ctx, Ty, Res.get(), P->hasAttr<HLSLInOutAttr>());
      auto *OpV = new (Ctx) OpaqueValueExpr(P->getLocStart(), Ty, VK_LValue,
                                            OK_Ordinary, OutExpr);
      Res = Sema.PerformImplicitConversion(OpV, Base->getType(),
                                           Sema::AA_Passing);
      if (Res.isInvalid())
        return ExprError();
      OutExpr->setWriteback(Res.get());
      OutExpr->setSrcLV(Base);
      OutExpr->setOpaqueValue(OpV);
      OpV->setSourceIsParent();
      return ExprResult(OutExpr);
    }

    DeclFinder DF;
    DF.Visit(Base);

    // If the analysis returned multiple possible decls, or no decl, or we've
    // seen the decl before, generate a HLSLOutParamExpr that can't be elided.
    if (DF.MultipleFound || DF.Decl == nullptr ||
        DF.Decl->getType().getQualifiers().hasAddressSpace() ||
        SeenVars.count(DF.Decl) > 0 || !DF.Decl->hasLocalStorage())
      return ExprResult(
          HLSLOutParamExpr::Create(Ctx, Ty, Base, P->hasAttr<HLSLInOutAttr>()));
    // Add the decl to the seen list, and generate a HLSLOutParamExpr that can
    // be elided.
    SeenVars.insert(DF.Decl);
    return ExprResult(HLSLOutParamExpr::Create(
        Ctx, Ty, Base, P->hasAttr<HLSLInOutAttr>(), true));
  }
};

} // namespace clang

#endif // CLANG_SEMA_HLSLOUTPARAMBUILDER_H

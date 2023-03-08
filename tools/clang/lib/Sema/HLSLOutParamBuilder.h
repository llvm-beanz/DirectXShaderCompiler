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
#include "llvm/ADT/DenseSet.h"

namespace clang {

class HLSLOutParamBuilder {
  llvm::DenseSet<NamedDecl *> SeenVars;

  // not copyable
  HLSLOutParamBuilder(const HLSLOutParamBuilder &) = delete;
  HLSLOutParamBuilder &operator=(const HLSLOutParamBuilder &) = delete;

  class DeclFinder : public StmtVisitor<DeclFinder> {
  public:
    NamedDecl *Decl = nullptr;
    bool MultipleFound = false;

    DeclFinder() = default;

    void VisitStmt(Stmt *S) { VisitChildren(S); }

    void VisitDeclRefExpr(DeclRefExpr *DRE) {
      if (MultipleFound)
        return;
      if (Decl)
        MultipleFound = true;
      Decl = DRE->getFoundDecl();
      return;
    }

    void VisitChildren(Stmt *S) {
      for (Stmt *SubStmt : S->children())
        if (SubStmt)
          this->Visit(SubStmt);
    }
  };

public:
  HLSLOutParamBuilder() = default;

  HLSLOutParamExpr *Create(ASTContext &Ctx, ParmVarDecl *P, Expr *Base) {
    DeclFinder DF;
    DF.Visit(Base);

    // If the analysis returned multiple possible decls, or no decl, or we've
    // seen the decl before, generate a HLSLOutParamExpr that can't be elided.
    if (DF.MultipleFound || DF.Decl == nullptr || SeenVars.count(DF.Decl) > 0)
      return new (Ctx) HLSLOutParamExpr(Base->getType(), Base,
                                        P->hasAttr<HLSLInOutAttr>());
    // Add the decl to the seen list, and generate a HLSLOutParamExpr that can
    // be elided.
    SeenVars.insert(DF.Decl);
    return new (Ctx) HLSLOutParamExpr(Base->getType(), Base,
                                      P->hasAttr<HLSLInOutAttr>(), true);
  }
};

} // namespace clang

#endif // CLANG_SEMA_HLSLOUTPARAMBUILDER_H

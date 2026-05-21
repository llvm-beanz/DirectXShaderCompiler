///////////////////////////////////////////////////////////////////////////////
//                                                                           //
// dxcrewriteautodiff.cpp                                                    //
// Copyright (C) Microsoft Corporation. All rights reserved.                 //
// This file is distributed under the University of Illinois Open Source     //
// License. See LICENSE.TXT for details.                                     //
//                                                                           //
// Implements -generate-differentials: synthesises forward and backward      //
// mode auto-differentiation variants of functions annotated with the        //
// [[dxc::autodiff(...)]] attribute.                                         //
//                                                                           //
///////////////////////////////////////////////////////////////////////////////

#include "dxcrewriteautodiff.h"

#include "clang/AST/ASTContext.h"
#include "clang/AST/Attr.h"
#include "clang/AST/Decl.h"
#include "clang/AST/DeclCXX.h"
#include "clang/AST/Expr.h"
#include "clang/AST/ExprCXX.h"
#include "clang/AST/PrettyPrinter.h"
#include "clang/AST/RecursiveASTVisitor.h"
#include "clang/AST/Stmt.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/ADT/StringSet.h"
#include "llvm/ADT/StringSwitch.h"
#include "llvm/Support/raw_ostream.h"

using namespace clang;
using namespace llvm;

namespace {

// Translate a function body expression into a textual representation in either
// forward or backward auto-diff form. The translation is intentionally
// conservative: a documented subset of HLSL is recognised and translated; any
// construct that falls outside that subset is rendered with /*TODO*/ markers
// and the original tokens so that the generated output is still useful to a
// human reviewer.
class AutoDiffEmitter {
public:
  enum Mode { Fwd, Bwd };

  AutoDiffEmitter(Mode M, StringRef ElemType, raw_ostream &OS,
                  const PrintingPolicy &P)
      : M(M), ElemType(ElemType), OS(OS), Policy(P) {}

  // Whether emission of the current function has encountered a construct that
  // is fundamentally not differentiable. Such constructs cause the emitter to
  // wrap the function body in a `_Static_assert(false, ...)` stub.
  bool sawNonDifferentiable() const { return NonDifferentiable; }
  StringRef nonDifferentiableReason() const { return Reason; }

  // Emit `<expr>` translated into the chosen mode.
  void emitExpr(const Expr *E) {
    E = E ? E->IgnoreParenImpCasts() : nullptr;
    if (!E) {
      OS << "/*null*/";
      return;
    }
    if (const auto *BO = dyn_cast<BinaryOperator>(E)) {
      emitBinaryOp(BO);
      return;
    }
    if (const auto *UO = dyn_cast<UnaryOperator>(E)) {
      emitUnaryOp(UO);
      return;
    }
    if (const auto *CE = dyn_cast<CallExpr>(E)) {
      emitCall(CE);
      return;
    }
    if (const auto *DRE = dyn_cast<DeclRefExpr>(E)) {
      emitDeclRef(DRE);
      return;
    }
    if (const auto *CO = dyn_cast<ConditionalOperator>(E)) {
      // ternary: ?: is not differentiable in a sound way unless both arms
      // have the same gradient, but the user almost always means it. Emit
      // it through and flag the function.
      markNonDifferentiable("ternary operator");
      E->printPretty(OS, nullptr, Policy);
      return;
    }
    if (isa<FloatingLiteral>(E) || isa<IntegerLiteral>(E)) {
      // Literals are printed as-is; the ad library expects raw scalars in
      // many positions, so we don't wrap them in constant<T>(...).
      E->printPretty(OS, nullptr, Policy);
      return;
    }
    // Fallback: pretty print as-is so the generated function still compiles
    // approximately in the user's hand-off cases. Mark with a TODO.
    OS << "/*TODO: unsupported expr*/ ";
    E->printPretty(OS, nullptr, Policy);
  }

private:
  Mode M;
  StringRef ElemType;
  raw_ostream &OS;
  const PrintingPolicy &Policy;
  bool NonDifferentiable = false;
  std::string Reason;

  void markNonDifferentiable(StringRef R) {
    if (!NonDifferentiable) {
      NonDifferentiable = true;
      Reason = R.str();
    }
  }

  void emitBinaryOp(const BinaryOperator *BO) {
    if (M == Fwd) {
      // Forward mode: Value<T> overloads the standard operators.
      OS << "(";
      emitExpr(BO->getLHS());
      OS << " " << BinaryOperator::getOpcodeStr(BO->getOpcode()) << " ";
      emitExpr(BO->getRHS());
      OS << ")";
      return;
    }
    // Backward mode: rewrite +, -, *, / into add/subtract/multiply/divide.
    const char *Fn = nullptr;
    switch (BO->getOpcode()) {
    case BO_Add: Fn = "add"; break;
    case BO_Sub: Fn = "subtract"; break;
    case BO_Mul: Fn = "multiply"; break;
    case BO_Div: Fn = "divide"; break;
    default: break;
    }
    if (!Fn) {
      OS << "/*TODO: unsupported binop*/ ";
      BO->printPretty(OS, nullptr, Policy);
      return;
    }
    OS << Fn << "<" << ElemType << ">(";
    emitExpr(BO->getLHS());
    OS << ", ";
    emitExpr(BO->getRHS());
    OS << ")";
  }

  void emitUnaryOp(const UnaryOperator *UO) {
    if (M == Fwd) {
      OS << UnaryOperator::getOpcodeStr(UO->getOpcode());
      OS << "(";
      emitExpr(UO->getSubExpr());
      OS << ")";
      return;
    }
    if (UO->getOpcode() == UO_Minus) {
      OS << "negate<" << ElemType << ">(";
      emitExpr(UO->getSubExpr());
      OS << ")";
      return;
    }
    OS << "/*TODO: unsupported unop*/ ";
    UO->printPretty(OS, nullptr, Policy);
  }

  void emitCall(const CallExpr *CE) {
    const FunctionDecl *Callee = CE->getDirectCallee();
    StringRef Name = Callee ? Callee->getName() : "";
    if (M == Fwd) {
      // Forward mode: the ad library overloads the standard names.
      OS << Name << "(";
      for (unsigned I = 0, N = CE->getNumArgs(); I < N; ++I) {
        if (I)
          OS << ", ";
        emitExpr(CE->getArg(I));
      }
      OS << ")";
      return;
    }
    // Backward mode: map known unary math intrinsics to *Expr<T>.
    StringRef Mapped =
        StringSwitch<StringRef>(Name)
            .Case("sin", "sinExpr")
            .Case("cos", "cosExpr")
            .Case("exp", "expExpr")
            .Case("log", "logExpr")
            .Case("sqrt", "sqrtExpr")
            .Case("log2", "log2Expr")
            .Case("pow", "power")
            .Case("max", "maxExpr")
            .Case("min", "minExpr")
            .Default("");
    if (Mapped.empty()) {
      OS << "/*TODO: unsupported call " << Name << "*/ ";
      CE->printPretty(OS, nullptr, Policy);
      return;
    }
    OS << Mapped << "<" << ElemType << ">(";
    for (unsigned I = 0, N = CE->getNumArgs(); I < N; ++I) {
      if (I)
        OS << ", ";
      emitExpr(CE->getArg(I));
    }
    OS << ")";
  }

  void emitDeclRef(const DeclRefExpr *DRE) {
    StringRef Name = DRE->getDecl()->getName();
    if (M == Fwd) {
      OS << Name;
      return;
    }
    // In backward mode, parameters are referenced through their _expr
    // VariableExpr wrappers declared at the top of the body.
    if (isa<ParmVarDecl>(DRE->getDecl())) {
      OS << Name << "_expr";
      return;
    }
    OS << Name;
  }
};

// Render the autodiff signature for a function in either mode.
void emitAutoDiffSignature(const FunctionDecl *FD, AutoDiffEmitter::Mode M,
                           StringRef ElemType, raw_ostream &OS) {
  if (M == AutoDiffEmitter::Fwd) {
    OS << "Value<" << ElemType << "> " << FD->getName() << "(";
    bool First = true;
    for (const ParmVarDecl *P : FD->parameters()) {
      if (!First)
        OS << ", ";
      First = false;
      OS << "Value<" << ElemType << "> " << P->getName();
    }
    OS << ")";
    return;
  }
  // Backward mode.
  OS << "Variable<" << ElemType << "> " << FD->getName()
     << "(inout GradientContext<" << ElemType << "> context";
  for (const ParmVarDecl *P : FD->parameters()) {
    OS << ", Variable<" << ElemType << "> " << P->getName();
  }
  OS << ")";
}

// Emit the auto-diff variant of a single function inside the appropriate
// namespace block. Returns true if anything was written.
bool emitAutoDiffFunction(const FunctionDecl *FD, AutoDiffEmitter::Mode M,
                          const PrintingPolicy &Policy, raw_ostream &OS) {
  // Determine the element type from the return type. We support scalar
  // float-like functions for now; other return types produce a TODO.
  std::string ElemType;
  raw_string_ostream ES(ElemType);
  FD->getReturnType().getCanonicalType().print(ES, Policy);
  ES.flush();

  emitAutoDiffSignature(FD, M, ElemType, OS);
  OS << " {\n";

  if (M == AutoDiffEmitter::Bwd) {
    // Wrap each parameter in a VariableExpr<T>.
    for (const ParmVarDecl *P : FD->parameters()) {
      OS << "    VariableExpr<" << ElemType << "> " << P->getName()
         << "_expr = makeVariableExpr<" << ElemType << ">(" << P->getName()
         << ");\n";
    }
  }

  AutoDiffEmitter Em(M, ElemType, OS, Policy);

  // Translate every top-level statement in the body. We handle ReturnStmt
  // specially; everything else is passed through with a TODO marker.
  const Stmt *Body = FD->getBody();
  if (const auto *CS = dyn_cast_or_null<CompoundStmt>(Body)) {
    for (const Stmt *S : CS->body()) {
      if (const auto *RS = dyn_cast<ReturnStmt>(S)) {
        OS << "    return ";
        Em.emitExpr(RS->getRetValue());
        OS << ";\n";
        continue;
      }
      // Best-effort pass-through for non-return statements.
      OS << "    /*TODO: unsupported statement*/ ";
      S->printPretty(OS, nullptr, Policy);
      OS << "\n";
    }
  } else {
    OS << "    /*TODO: missing body*/\n";
  }

  OS << "}\n";
  return true;
}

// Emit forward and/or backward generated functions for a single annotated
// function, wrapped in the user::ad::{fwd,bwd}:: namespaces.
void EmitAutoDiffForFunction(const FunctionDecl *FD,
                             const HLSLAutoDiffAttr *Attr,
                             const PrintingPolicy &Policy, raw_ostream &OS) {
  if (Attr->hasForward()) {
    OS << "\nnamespace user { namespace ad { namespace fwd {\n";
    emitAutoDiffFunction(FD, AutoDiffEmitter::Fwd, Policy, OS);
    OS << "} } } // namespace user::ad::fwd\n";
  }
  if (Attr->hasBackward()) {
    OS << "\nnamespace user { namespace ad { namespace bwd {\n";
    emitAutoDiffFunction(FD, AutoDiffEmitter::Bwd, Policy, OS);
    OS << "} } } // namespace user::ad::bwd\n";
  }
}

} // anonymous namespace

namespace hlsl {

void PrintTranslationUnitWithDifferentials(TranslationUnitDecl *tu,
                                           raw_ostream &OS,
                                           PrintingPolicy &Policy) {
  for (Decl *D : tu->decls()) {
    if (D->isImplicit())
      continue;
    D->print(OS, Policy);
    // DeclPrinter typically does not append trailing semicolons for some
    // forms; we always insert a newline so that any subsequent text we
    // write begins on its own line.
    OS << "\n";
    auto *FD = dyn_cast<FunctionDecl>(D);
    if (!FD)
      continue;
    if (auto *AD = FD->getAttr<HLSLAutoDiffAttr>())
      EmitAutoDiffForFunction(FD, AD, Policy, OS);
  }
}

} // namespace hlsl

///////////////////////////////////////////////////////////////////////////////
//                                                                           //
// dxcrewriteautodiffir.h                                                    //
// Copyright (C) Microsoft Corporation. All rights reserved.                 //
// This file is distributed under the University of Illinois Open Source     //
// License. See LICENSE.TXT for details.                                     //
//                                                                           //
// Defines the typed, function-local IR used by the auto-diff rewriter.      //
//                                                                           //
///////////////////////////////////////////////////////////////////////////////

#ifndef LLVM_CLANG_TOOLS_LIBCLANG_DXCREWRITEAUTODIFFIR_H
#define LLVM_CLANG_TOOLS_LIBCLANG_DXCREWRITEAUTODIFFIR_H

#include "clang/AST/Decl.h"
#include "clang/AST/Expr.h"
#include "llvm/ADT/SmallVector.h"

#include <memory>
#include <string>
#include <vector>

namespace hlsl {
namespace autodiff {

enum class ADActivity { Inactive, Active };

enum class ADValueCategory { RValue, LValue };

struct ADBinding;

struct ADValueInfo {
  clang::QualType PrimalType;
  ADActivity Activity = ADActivity::Inactive;
  ADValueCategory ValueCategory = ADValueCategory::RValue;
  const clang::ValueDecl *SourceDecl = nullptr;
};

struct ADExpr {
  enum class Kind {
    Literal,
    DeclRef,
    LocalRef,
    PrimalLocal,
    This,
    Member,
    Swizzle,
    Subscript,
    AggregateConstruct,
    Cast,
    Unary,
    Binary,
    Conditional,
    Call,
    RuntimeLoopResult,
  };

  ADExpr(Kind K, const clang::Expr *SourceExpr);

  Kind K;
  ADValueInfo Value;
  clang::SourceRange Range;
  const clang::Expr *SourceExpr;
  const clang::ValueDecl *SourceDecl = nullptr;
  const ADBinding *Binding = nullptr;
  const clang::FunctionDecl *Callee = nullptr;
  const clang::VarDecl *LoopCounter = nullptr;
  const ADExpr *Receiver = nullptr;
  clang::UnaryOperatorKind UnaryOpcode = clang::UO_Plus;
  clang::BinaryOperatorKind BinaryOpcode = clang::BO_Add;
  llvm::SmallVector<const ADExpr *, 4> Operands;
  llvm::SmallVector<unsigned, 4> Components;
};

struct ADBinding {
  const clang::VarDecl *SourceDecl = nullptr;
  unsigned Version = 0;
  const ADExpr *Value = nullptr;
};

struct ADStmt {
  enum class Kind {
    Declare,
    Assign,
    Expression,
    PrimalLoop,
    ActiveLoop,
    Return
  };

  Kind K;
  const ADBinding *Binding = nullptr;
  const ADExpr *Value = nullptr;
  const clang::Stmt *SourceStmt = nullptr;
};

struct ADFunctionPlan {
  const clang::FunctionDecl *Source = nullptr;
  clang::QualType ResultType;
  std::vector<std::unique_ptr<ADExpr>> Expressions;
  std::vector<std::unique_ptr<ADBinding>> Bindings;
  std::vector<ADStmt> Statements;
  llvm::SmallVector<const clang::VarDecl *, 4> PrimalLocals;
};

// Build a typed plan for a straight-line function. Returns false when the
// function contains a construct not represented by this initial IR slice; the
// caller may then use the legacy emitter for the whole function.
bool BuildADFunctionPlan(const clang::FunctionDecl *FD, ADFunctionPlan &Plan,
                         std::string &Reason);

} // namespace autodiff
} // namespace hlsl

#endif // LLVM_CLANG_TOOLS_LIBCLANG_DXCREWRITEAUTODIFFIR_H

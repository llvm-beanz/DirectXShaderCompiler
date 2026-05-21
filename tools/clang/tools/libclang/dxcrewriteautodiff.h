///////////////////////////////////////////////////////////////////////////////
//                                                                           //
// dxcrewriteautodiff.h                                                      //
// Copyright (C) Microsoft Corporation. All rights reserved.                 //
// This file is distributed under the University of Illinois Open Source     //
// License. See LICENSE.TXT for details.                                     //
//                                                                           //
// Declares the auto-differentiation pass used by the DXC rewriter when      //
// invoked with -generate-differentials.                                     //
//                                                                           //
///////////////////////////////////////////////////////////////////////////////

#ifndef LLVM_CLANG_TOOLS_LIBCLANG_DXCREWRITEAUTODIFF_H
#define LLVM_CLANG_TOOLS_LIBCLANG_DXCREWRITEAUTODIFF_H

namespace clang {
class TranslationUnitDecl;
struct PrintingPolicy;
} // namespace clang

namespace llvm {
class raw_ostream;
} // namespace llvm

namespace hlsl {

// Walk a translation unit, printing each top-level declaration normally and
// emitting auto-diff stubs immediately after any function carrying the
// HLSLAutoDiffAttr. Implementation lives in dxcrewriteautodiff.cpp.
void PrintTranslationUnitWithDifferentials(clang::TranslationUnitDecl *tu,
                                           llvm::raw_ostream &OS,
                                           clang::PrintingPolicy &Policy);

} // namespace hlsl

#endif // LLVM_CLANG_TOOLS_LIBCLANG_DXCREWRITEAUTODIFF_H

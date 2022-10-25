///////////////////////////////////////////////////////////////////////////////
//                                                                           //
// IntermediateMetadata.h                                                    //
// Copyright (C) Microsoft Corporation. All rights reserved.                 //
// This file is distributed under the University of Illinois Open Source     //
// License. See LICENSE.TXT for details.                                     //
//                                                                           //
// Representation of intermediate metadata used to pass data from the        //
// frontend to the middle end.                                               //
//                                                                           //
///////////////////////////////////////////////////////////////////////////////

#ifndef DXC_HLSL_INTERMEDIATEMETADATA_H
#define DXC_HLSL_INTERMEDIATEMETADATA_H

#include "llvm/IR/Constants.h"
#include "llvm/IR/Metadata.h"
#include "llvm/IR/Module.h"
#include "llvm/Support/Casting.h"

namespace hlsl {

class LangStdMD {
  llvm::NamedMDNode *Entry;

  LangStdMD(llvm::NamedMDNode *E) : Entry(E) {}

public:
  LangStdMD(llvm::Module &M) : Entry(M.getNamedMetadata("hlsl.langstd")) {}

  LangStdMD(LangStdMD &) = default;

  static void create(llvm::Module &M, uint32_t Version) {
    auto &Ctx = M.getContext();
    auto *LangVer =
        llvm::ConstantInt::get(llvm::Type::getInt32Ty(Ctx), Version);
    llvm::NamedMDNode *HLSLLangStd = M.getOrInsertNamedMetadata("hlsl.langstd");
    HLSLLangStd->addOperand(
        llvm::MDNode::get(Ctx, {llvm::ConstantAsMetadata::get(LangVer)}));
  }

  uint32_t getLangVersion() const {
    return llvm::cast<llvm::ConstantInt>(
        llvm::cast<llvm::ConstantAsMetadata>(Entry->getOperand(0)->getOperand(0))
            ->getValue())
        ->getLimitedValue();
  }

  void erase() { Entry->eraseFromParent(); }
};

} // namespace hlsl

#endif // DXC_HLSL_INTERMEDIATEMETADATA_H

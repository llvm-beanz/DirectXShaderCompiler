///////////////////////////////////////////////////////////////////////////////
//                                                                           //
// DxilStringLowering.cpp                                                    //
// Copyright (C) Microsoft Corporation. All rights reserved.                 //
// This file is distributed under the University of Illinois Open Source     //
// License. See LICENSE.TXT for details.                                     //
///////////////////////////////////////////////////////////////////////////////

#include "dxc/DXIL/DxilModule.h"
#include "dxc/HLSL/DxilGenerationPass.h"
#include "llvm/ADT/SmallString.h"
#include "llvm/CodeGen/Passes.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/Instruction.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/IntrinsicInst.h"
#include "llvm/IR/Intrinsics.h"
#include "llvm/IR/Module.h"
#include "llvm/InitializePasses.h"
#include "llvm/MC/StringTableBuilder.h"
#include "llvm/Pass.h"

#define DEBUG_TYPE "dxil-string-lowering"

using namespace llvm;

namespace {

class DXILStringLoweringModule : public ModulePass {

  StringTableBuilder StrTabBuilder;

  void addString(ConstantDataArray *CDA) {
    StringRef Str = CDA->getAsString();
    StrTabBuilder.add(Str);
  }

  Value *convertStringToOffset(StringRef Str, IRBuilder<> &Builder) {
    size_t Offset = StrTabBuilder.getOffset(Str);
    Constant *CastPtr = ConstantInt::get(Builder.getInt32Ty(), Offset);
    return CastPtr;
  }

public:
  bool runOnModule(Module &M) override {
    SmallVector<std::pair<IntrinsicInst *, StringRef>, 16> WorkList;
    for (Function &F : M)
      for (BasicBlock &BB : F)
        for (Instruction &I : BB)
          if (auto *II = dyn_cast<IntrinsicInst>(&I))
            if (II->getIntrinsicID() == Intrinsic::string_to_offset) {
              Value *StrOperand = II->getArgOperand(0);
              if (auto *GEP = dyn_cast<GEPOperator>(StrOperand)) {
                StrOperand = GEP->getOperand(0);
                if (GEP->getNumIndices() != 2 || !GEP->hasAllZeroIndices())
                  report_fatal_error("llvm.string.to.offset argument must be a "
                                     "constant string");
              }
              if (auto *GV = dyn_cast<GlobalVariable>(StrOperand))
                StrOperand = GV->getInitializer();
              ConstantDataArray *CVA = dyn_cast<ConstantDataArray>(StrOperand);
              if (!CVA)
                report_fatal_error(
                    "llvm.string.to.offset argument must be a constant string");
              addString(CVA);
              WorkList.push_back(std::make_pair(II, CVA->getAsString()));
            }
    if (WorkList.empty())
      return false;

    StrTabBuilder.finalize(StringTableBuilder::ELF);
    M.GetDxilModule().SetStringTable(StrTabBuilder.data());

    for (auto Item : WorkList) {
      IRBuilder<> Builder(Item.first);
      Value *OffsetPtr = convertStringToOffset(Item.second, Builder);
      Item.first->replaceAllUsesWith(OffsetPtr);
      Item.first->eraseFromParent();
    }

    return true;
  }

  DXILStringLoweringModule() : ModulePass(ID), StrTabBuilder() {}
  static char ID; // Pass identification.
};
char DXILStringLoweringModule::ID = 0;

} // end anonymous namespace

INITIALIZE_PASS(DXILStringLoweringModule, DEBUG_TYPE,
                "DXIL String Lowering Module", false, false)

ModulePass *llvm::createDXILStringLoweringModulePass() {
  return new DXILStringLoweringModule();
}

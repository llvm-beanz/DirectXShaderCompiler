///////////////////////////////////////////////////////////////////////////////
//                                                                           //
// DxilStringLowering.cpp                                                    //
// Copyright (C) Microsoft Corporation. All rights reserved.                 //
// This file is distributed under the University of Illinois Open Source     //
// License. See LICENSE.TXT for details.                                     //
///////////////////////////////////////////////////////////////////////////////

#include "dxc/DXIL/DxilModule.h"
#include "dxc/DXIL/DxilShaderModel.h"
#include "dxc/DXIL/DxilUtil.h"
#include "dxc/HLSL/DxilGenerationPass.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/SetVector.h"
#include "llvm/ADT/SmallPtrSet.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/Analysis/ValueTracking.h"
#include "llvm/CodeGen/Passes.h"
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

  bool collectStrings(Value *Root, SmallSetVector<Value *, 16> &StringValues,
                      DenseMap<Value *, StringRef> &Strings) {
    SmallVector<Value *, 16> Pending(1, Root);
    SmallVector<Value *, 16> Values;
    SmallPtrSet<Value *, 32> Visited;
    bool HasLiteral = false;
    while (!Pending.empty()) {
      Value *V = Pending.pop_back_val();
      if (StringValues.count(V)) {
        HasLiteral = true;
        continue;
      }
      if (!Visited.insert(V).second)
        continue;
      Values.push_back(V);
      if (auto *Phi = dyn_cast<PHINode>(V)) {
        for (Value *Incoming : Phi->incoming_values())
          Pending.push_back(Incoming);
      } else if (auto *Select = dyn_cast<SelectInst>(V)) {
        Pending.push_back(Select->getFalseValue());
        Pending.push_back(Select->getTrueValue());
      } else {
        StringRef Str;
        if (!getConstantStringInfo(V, Str, 0, /*TrimAtNul=*/false))
          return false;
        Strings[V] = Str;
        StrTabBuilder.add(Str);
        HasLiteral = true;
      }
    }
    if (!HasLiteral)
      return false;
    StringValues.insert(Values.begin(), Values.end());
    return true;
  }

  void removeDeadStringNodes(const SmallSetVector<Value *, 16> &StringValues) {
    SmallPtrSet<Instruction *, 32> DeadNodes;
    SmallVector<Instruction *, 16> Pending;
    for (Value *V : StringValues)
      if (isa<PHINode>(V) || isa<SelectInst>(V))
        DeadNodes.insert(cast<Instruction>(V));

    for (Instruction *I : DeadNodes)
      for (User *U : I->users())
        if (!DeadNodes.count(dyn_cast<Instruction>(U))) {
          Pending.push_back(I);
          break;
        }
    while (!Pending.empty()) {
      Instruction *I = Pending.pop_back_val();
      if (!DeadNodes.erase(I))
        continue;
      for (Value *Operand : I->operands())
        if (auto *Op = dyn_cast<Instruction>(Operand))
          if (DeadNodes.count(Op))
            Pending.push_back(Op);
    }

    // Drop references together so dead loop-carried pointer cycles disappear.
    for (Instruction *I : DeadNodes)
      I->dropAllReferences();
    for (Instruction *I : DeadNodes)
      I->eraseFromParent();
  }

public:
  bool runOnModule(Module &M) override {
    StrTabBuilder.clear();
    SmallVector<IntrinsicInst *, 16> WorkList;
    SmallSetVector<Value *, 16> StringValues;
    DenseMap<Value *, StringRef> Strings;
    bool HasErrors = false;
    for (Function &F : M)
      for (BasicBlock &BB : F)
        for (Instruction &I : BB)
          if (auto *II = dyn_cast<IntrinsicInst>(&I))
            if (II->getIntrinsicID() == Intrinsic::string_to_offset) {
              if (M.GetDxilModule().GetShaderModel()->IsLib()) {
                hlsl::dxilutil::EmitErrorOnInstruction(
                    II,
                    "string-to-offset is not supported in library profiles");
                HasErrors = true;
              } else if (!collectStrings(II->getArgOperand(0), StringValues,
                                         Strings)) {
                hlsl::dxilutil::EmitErrorOnInstruction(
                    II, "string-to-offset argument must resolve to constant "
                        "strings through select or phi nodes");
                HasErrors = true;
              }
              WorkList.push_back(II);
            }
    if (WorkList.empty())
      return false;

    Type *I32Ty = Type::getInt32Ty(M.getContext());
    DenseMap<Value *, Value *> Offsets;
    if (!HasErrors) {
      StrTabBuilder.finalize(StringTableBuilder::ELF);
      M.GetDxilModule().SetStringTable(StrTabBuilder.data());

      // Create all nodes before wiring operands, including select/phi cycles.
      for (Value *V : StringValues) {
        if (auto *Phi = dyn_cast<PHINode>(V)) {
          auto *OffsetPhi = PHINode::Create(I32Ty, Phi->getNumIncomingValues(),
                                            Phi->getName() + ".offset", Phi);
          OffsetPhi->setDebugLoc(Phi->getDebugLoc());
          Offsets[V] = OffsetPhi;
        } else if (auto *Select = dyn_cast<SelectInst>(V)) {
          auto *OffsetSelect = SelectInst::Create(
              Select->getCondition(), UndefValue::get(I32Ty),
              UndefValue::get(I32Ty), Select->getName() + ".offset", Select);
          OffsetSelect->setDebugLoc(Select->getDebugLoc());
          Offsets[V] = OffsetSelect;
        } else {
          Offsets[V] = ConstantInt::get(
              I32Ty, StrTabBuilder.getOffset(Strings.lookup(V)));
        }
      }
      for (Value *V : StringValues) {
        if (auto *Phi = dyn_cast<PHINode>(V)) {
          auto *OffsetPhi = cast<PHINode>(Offsets.lookup(V));
          for (unsigned I = 0; I < Phi->getNumIncomingValues(); ++I)
            OffsetPhi->addIncoming(Offsets.lookup(Phi->getIncomingValue(I)),
                                   Phi->getIncomingBlock(I));
        } else if (auto *Select = dyn_cast<SelectInst>(V)) {
          auto *OffsetSelect = cast<SelectInst>(Offsets.lookup(V));
          OffsetSelect->setOperand(1, Offsets.lookup(Select->getTrueValue()));
          OffsetSelect->setOperand(2, Offsets.lookup(Select->getFalseValue()));
        }
      }
    }

    Function *StringToOffset = WorkList.front()->getCalledFunction();
    for (IntrinsicInst *II : WorkList) {
      Value *Offset = HasErrors ? UndefValue::get(I32Ty)
                                : Offsets.lookup(II->getArgOperand(0));
      II->replaceAllUsesWith(Offset);
      II->eraseFromParent();
    }
    if (!HasErrors)
      removeDeadStringNodes(StringValues);
    if (StringToOffset->use_empty())
      StringToOffset->eraseFromParent();

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

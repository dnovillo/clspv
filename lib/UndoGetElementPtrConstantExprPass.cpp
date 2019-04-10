// Copyright 2017 The Clspv Authors. All rights reserved.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include "llvm/IR/Constants.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/Module.h"
#include "llvm/Pass.h"
#include "llvm/Transforms/Utils/Cloning.h"

#include "Passes.h"

using namespace llvm;

#define DEBUG_TYPE "UndoGetElementPtrConstantExpr"

namespace {
struct UndoGetElementPtrConstantExprPass : public ModulePass {
  static char ID;
  UndoGetElementPtrConstantExprPass() : ModulePass(ID) {}

  bool runOnModule(Module &M) override;

  bool replaceGetElementPtrConstantExpr(ConstantExpr *CE);
};
} // namespace

char UndoGetElementPtrConstantExprPass::ID = 0;
INITIALIZE_PASS(UndoGetElementPtrConstantExprPass,
                "UndoGetElementPtrConstantExpr", "Undo GEP Constant Expr Pass",
                false, false)

namespace clspv {
ModulePass *createUndoGetElementPtrConstantExprPass() {
  return new UndoGetElementPtrConstantExprPass();
}
} // namespace clspv

bool UndoGetElementPtrConstantExprPass::runOnModule(Module &M) {
  bool changed = false;

  for (GlobalVariable &GV : M.globals()) {
    // Walk the users of the global variable.
    const SmallVector<User *, 8> GVUsers(GV.user_begin(), GV.user_end());
    for (User *U : GVUsers) {
      if (ConstantExpr *CE = dyn_cast<ConstantExpr>(U)) {
        if (Instruction::GetElementPtr == CE->getOpcode()) {
          changed |= replaceGetElementPtrConstantExpr(CE);
        }
      }
    }
  }

  return changed;
}

bool UndoGetElementPtrConstantExprPass::replaceGetElementPtrConstantExpr(
    ConstantExpr *CE) {
  SmallVector<Instruction *, 8> WorkList;

  // Walk the users of the constant expression.
  for (User *U : CE->users()) {
    if (Instruction *I = dyn_cast<Instruction>(U)) {
      WorkList.push_back(I);
    }
  }

  for (Instruction *I : WorkList) {
    // Create the instruction equivalent of the constant expression.
    Instruction *NewI = CE->getAsInstruction();

    if (PHINode *PHI = dyn_cast<PHINode>(I)) {
      // If PHINode uses CE, put new instruction of CE at end of the incoming
      // block.
      for (unsigned i = 0; i < PHI->getNumIncomingValues(); i++) {
        if (PHI->getIncomingValue(i) == CE) {
          NewI->insertBefore(PHI->getIncomingBlock(i)->getTerminator());
        }
      }
    } else {
      // Insert it just before the instruction that will use it.
      NewI->insertBefore(I);
    }

    // Walk the operands of the instruction to find where the constant
    // expression was used.
    for (unsigned i = 0; i < I->getNumOperands(); i++) {
      if (CE == I->getOperand(i)) {
        I->setOperand(i, NewI);
        break;
      }
    }
  }

  if (CE->user_empty())
    CE->destroyConstant();

  return true;
}

#include <sstream>
#include <iostream>
#include "llvm/Pass.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/InstIterator.h"
#include "llvm/ADT/SmallPtrSet.h"
#include "llvm/Support/raw_ostream.h"
#include "TaffoInitializerPass.h"
#include "AnnotationParser.h"
#include "Metadata.h"

using namespace llvm;
using namespace taffo;


void TaffoInitializer::readGlobalAnnotations(Module &m, SmallPtrSetImpl<Value *>& variables,
					     bool functionAnnotation)
{
  GlobalVariable *globAnnos = m.getGlobalVariable("llvm.global.annotations");

  if (globAnnos != NULL)
  {
    if (ConstantArray *annos = dyn_cast<ConstantArray>(globAnnos->getInitializer()))
    {
      for (unsigned i = 0, n = annos->getNumOperands(); i < n; i++)
      {
        if (ConstantStruct *anno = dyn_cast<ConstantStruct>(annos->getOperand(i)))
        {
          /*struttura expr (operando 0 contiene expr) :
            [OpType] operandi :
            [BitCast] *funzione , [GetElementPtr] *annotazione ,
            [GetElementPtr] *filename , [Int] linea di codice sorgente) */
          if (ConstantExpr *expr = dyn_cast<ConstantExpr>(anno->getOperand(0)))
          {
            if (expr->getOpcode() == Instruction::BitCast && (functionAnnotation ^ !isa<Function>(expr->getOperand(0))) )
            {
              parseAnnotation(variables, cast<ConstantExpr>(anno->getOperand(1)), expr->getOperand(0));
            }
          }
        }
      }
    }
  }
  if (functionAnnotation)
    removeNoFloatTy(variables);
}


void TaffoInitializer::readLocalAnnotations(llvm::Function &f, llvm::SmallPtrSetImpl<llvm::Value *> &variables)
{
  bool found = false;
  for (inst_iterator iIt = inst_begin(&f), iItEnd = inst_end(&f); iIt != iItEnd; iIt++) {
    if (CallInst *call = dyn_cast<CallInst>(&(*iIt))) {
      if (!call->getCalledFunction())
        continue;

      if (call->getCalledFunction()->getName() == "llvm.var.annotation") {
	bool isTarget = false;
        parseAnnotation(variables, cast<ConstantExpr>(iIt->getOperand(1)), iIt->getOperand(0), &isTarget);
	found |= isTarget;
      }
    }
  }
  if (found) {
    mdutils::MetadataManager::setStartingPoint(f);
  }
}


void TaffoInitializer::readAllLocalAnnotations(llvm::Module &m, llvm::SmallPtrSetImpl<llvm::Value *> &res)
{
  for (Function &f: m.functions()) {
    SmallPtrSet<Value*, 32> t;
    readLocalAnnotations(f, t);
    res.insert(t.begin(), t.end());

    /* Otherwise dce pass ignore the function
     * (removed also where it's not required) */
    f.removeFnAttr(Attribute::OptimizeNone);
  }
}

// Return true on success, false on error
bool TaffoInitializer::parseAnnotation(SmallPtrSetImpl<Value *>& variables,
				       ConstantExpr *annoPtrInst, Value *instr,
				       bool *isTarget)
{
  ValueInfo vi;

  if (!(annoPtrInst->getOpcode() == Instruction::GetElementPtr))
    return false;
  GlobalVariable *annoContent = dyn_cast<GlobalVariable>(annoPtrInst->getOperand(0));
  if (!annoContent)
    return false;
  ConstantDataSequential *annoStr = dyn_cast<ConstantDataSequential>(annoContent->getInitializer());
  if (!annoStr)
    return false;
  if (!(annoStr->isString()))
    return false;

  StringRef annstr = annoStr->getAsString();
  AnnotationParser parser;
  if (!parser.parseAnnotationString(annstr)) {
    errs() << "TAFFO annnotation parser syntax error: \n";
    errs() << "  In annotation: \"" << annstr << "\"\n";
    errs() << "  " << parser.lastError() << "\n";
    return false;
  }
  vi.fixpTypeRootDistance = 0;
  vi.isRoot = true;
  vi.isBacktrackingNode = parser.backtracking;
  vi.metadata = parser.metadata;
  if (isTarget)
    *isTarget = parser.target.hasValue();
  vi.target = parser.target;

  if (Instruction *toconv = dyn_cast<Instruction>(instr)) {
    variables.insert(toconv->getOperand(0));
    *valueInfo(toconv->getOperand(0)) = vi;
    
  } else if (Function *fun = dyn_cast<Function>(instr)) {
    enabledFunctions.insert(fun);
    for (auto user: fun->users()) {
      if (!(isa<CallInst>(user) || isa<InvokeInst>(user)))
        continue;
      variables.insert(user);
      *valueInfo(user) = vi;
    }
    
  } else {
    variables.insert(instr);
    *valueInfo(instr) = vi;
  }

  return true;
}


void TaffoInitializer::removeNoFloatTy(SmallPtrSetImpl<Value *> &res)
{
  for (auto it: res) {
    Type *ty;

    if (AllocaInst *alloca = dyn_cast<AllocaInst>(it)) {
      ty = alloca->getAllocatedType();
    } else if (GlobalVariable *global = dyn_cast<GlobalVariable>(it)) {
      ty = global->getType();
    } else if (isa<CallInst>(it) || isa<InvokeInst>(it)) {
      ty = it->getType();
      if (ty->isVoidTy())
        continue;
    } else {
      DEBUG(dbgs() << "annotated instruction " << *it <<
        " not an alloca or a global or a call/invoke, ignored\n");
      res.erase(it);
      continue;
    }

    while (ty->isArrayTy() || ty->isPointerTy()) {
      if (ty->isPointerTy())
        ty = ty->getPointerElementType();
      else
        ty = ty->getArrayElementType();
    }
    if (!ty->isFloatingPointTy()) {
      DEBUG(dbgs() << "annotated instruction " << *it << " does not allocate a"
        " kind of float; ignored\n");
      res.erase(it);
    }
  }
}

void TaffoInitializer::printAnnotatedObj(Module &m)
{
  SmallPtrSet<Value*, 32> res;

  readGlobalAnnotations(m, res, true);
  errs() << "Annotated Function: \n";
  if(!res.empty())
  {
    for (auto it : res)
    {
      errs() << " -> " << *it << "\n";
    }
    errs() << "\n";
  }

  res.clear();
  readGlobalAnnotations(m, res);
  errs() << "Global Set: \n";
  if(!res.empty())
  {
    for (auto it : res)
    {
      errs() << " -> " << *it << "\n";
    }
    errs() << "\n";
  }

  for (auto fIt=m.begin() , fItEnd=m.end() ; fIt!=fItEnd ; fIt++)
  {
    Function &f = *fIt;
    errs().write_escaped(f.getName()) << " : ";
    res.clear();
    readLocalAnnotations(f, res);
    if(!res.empty())
    {
      errs() << "\nLocal Set: \n";
      for (auto it : res)
      {
        errs() << " -> " << *it << "\n";
      }
    }
    errs() << "\n";
  }

}


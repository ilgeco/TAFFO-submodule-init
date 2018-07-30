#include "llvm/Pass.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/Constants.h"
#include "llvm/ADT/SmallPtrSet.h"
#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/Support/Debug.h"
#include "llvm/ADT/Statistic.h"
#include "llvm/Support/CommandLine.h"
#include "FixedPointType.h"


#ifndef __TAFFO_INITIALIZER_PASS_H__
#define __TAFFO_INITIALIZER_PASS_H__


#define DEBUG_TYPE "flttofix"
#define DEBUG_ANNOTATION "annotation"


STATISTIC(AnnotationCount, "Number of valid annotations found");


namespace taffo {


struct ValueInfo {
  bool isBacktrackingNode;
  bool isRoot;
  llvm::SmallPtrSet<llvm::Value*, 5> roots;
  FixedPointType fixpType;  // significant iff origType is a float or a pointer to a float
  int fixpTypeRootDistance = INT_MAX;
  llvm::Type *origType;
};


struct TaffoInitializer : public llvm::ModulePass {
  static char ID;
  
  llvm::DenseMap<llvm::Value *, ValueInfo> info;
  
  TaffoInitializer(): ModulePass(ID) { }
  bool runOnModule(llvm::Module &M) override;
  
  void readGlobalAnnotations(llvm::Module &m, llvm::SmallPtrSetImpl<llvm::Value *>& res, bool functionAnnotation = false);
  void readLocalAnnotations(llvm::Function &f, llvm::SmallPtrSetImpl<llvm::Value *>& res);
  void readAllLocalAnnotations(llvm::Module &m, llvm::SmallPtrSetImpl<llvm::Value *>& res);
  bool parseAnnotation(llvm::SmallPtrSetImpl<llvm::Value *>& variables, llvm::ConstantExpr *annoPtrInst, llvm::Value *instr);
  void removeNoFloatTy(llvm::SmallPtrSetImpl<llvm::Value *>& res);
  void printAnnotatedObj(llvm::Module &m);
  
  void buildConversionQueueForRootValues(const llvm::ArrayRef<llvm::Value*>& val, std::vector<llvm::Value*>& res);
  void printConversionQueue(std::vector<llvm::Value*> vals);
  void removeAnnotationCalls(std::vector<llvm::Value*>& vals);
  
  void setMetadataOfValue(llvm::Value *v);
};


}


#endif

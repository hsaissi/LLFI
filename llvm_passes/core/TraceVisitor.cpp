#include "TraceVisitor.h"

// I have no idea if this is safe? I hate pointers!!
Instruction *TraceVisitor::getAllocaInsertPoint(Instruction *I) {
  return I->getParent()->getParent()->begin()->getFirstNonPHIOrDbgOrLifetime();
}

TraceVisitor::TraceVisitor(LLVMContext *ctxt, Module *mod, DataLayout *dl,
                           int mtrace)
    : InstVisitor<TraceVisitor>() {
  context = ctxt;
  module = mod;
  dataLayout = dl;
  maxtrace = mtrace;
}
int TraceVisitor::getSize(Type *type) {
  return dataLayout->getTypeAllocSize(type);
  //  float bitSize = (float)dataLayout->getTypeSizeInBits(type);
  //  return (int)ceil(bitSize / 8.0);
}

AllocaInst *
TraceVisitor::insertInstrumentation(Value *val, Type *type,
                                    Instruction *insertPoint,
                                    Instruction *alloca_insertPoint) {
  float bitSize;
  AllocaInst *aInst;
  //  Type* type = val->getType();
  if (type != Type::getVoidTy(*context)) {
    // insert an instruction Allocate stack memory to store/pass
    // instruction value
    aInst = new AllocaInst(type, "llfi_trace", alloca_insertPoint);
    // Insert an instruction to Store the instruction Value!
    new StoreInst(val, aInst, insertPoint);
    // DataLayout &td = getAnalysis<DataLayout>();
    bitSize = (float)dataLayout->getTypeSizeInBits(type);
  } else {
    aInst = new AllocaInst(Type::getInt32Ty(*context), "llfi_trace",
                           alloca_insertPoint);
    new StoreInst(ConstantInt::get(IntegerType::get(*context, 32), 0), aInst,
                  insertPoint);
    bitSize = 32;
    // errs() << "was here --> " << *type << "\n";
    // errs() << "Check this --> " << *aInst->getType() << "\n";
  }
  int byteSize = (int)ceil(bitSize / 8.0);
  // errs() << "Size for " << *val << ": " << byteSize << "\n";
  return aInst;
}
AllocaInst * TraceVisitor::insertStringInstrumentation(std::string& str, Instruction* insertPoint, Instruction* alloca_insertPoint) {
  const char *content = str.c_str();
  ArrayRef<uint8_t> array_ref((uint8_t *) content,
                                          str.size() + 1);
  // llvm::Value* OPCodeName = llvm::ConstantArray::get(context,
  // opcode_name_array_ref);
  llvm::Value *value =
      llvm::ConstantDataArray::get(*context, array_ref);
  /********************************/

  AllocaInst *aInst =
      new AllocaInst(value->getType(), "llfi_trace", alloca_insertPoint);
  new StoreInst(value, aInst, insertPoint);
  return aInst;
}
AllocaInst *TraceVisitor::insertOpCode(Instruction *inst,
                                       Instruction *insertPoint,
                                       Instruction *alloca_insertPoint) {
  // Insert instructions to allocate stack memory for opcode name

  //  const char *tmpName = inst->getOpcodeName();
  std::string str(inst->getOpcodeName());
  if (isa<CallInst>(inst)) {
    CallInst *ci = dyn_cast<CallInst>(inst);
    std::string name(ci->getCalledFunction()->getName());
    str = str + "-" + name;
  }
  return insertStringInstrumentation(str, insertPoint, alloca_insertPoint);
}

void TraceVisitor::insertCall(Instruction *inst, Instruction *opCodeInst, Instruction* typeString,
                              std::vector<AllocaInst *> &parameters,
                              Instruction *insertPoint) {

  // Create the decleration of the printInstTracer Function
  // TODO
  std::vector<Value *> values(parameters.size() * 2 + 5);
  ConstantInt *IDConstantInt = ConstantInt::get(IntegerType::get(*context, 32),
                                                llfi::getLLFIIndexofInst(inst));
  ConstantInt *maxTraceConstInt =
      ConstantInt::get(IntegerType::get(*context, 32), maxtrace);
  ConstantInt *countConstInt =
      ConstantInt::get(IntegerType::get(*context, 32), parameters.size());
  values[0] = IDConstantInt;
  values[1] = opCodeInst;
  values[2] = maxTraceConstInt;
  values[3] = countConstInt;
  values[4] = typeString; 
  for (std::size_t i = 0; i != parameters.size(); i++) {
    values[2 * i + 5] = parameters[i];
    int bytesize = getSize((parameters[i])->getAllocatedType());
    //  errs() << bytesize << " ---------- " << *(parameters[i]->getType()) <<
    //  "\n";
    values[2 * i + 6] =
        ConstantInt::get(IntegerType::get(*context, 32), bytesize);
  }

  std::vector<Type *> parameterVector(values.size());
  for (std::size_t i = 0; i != values.size(); i++) {
    parameterVector[i] = values[i]->getType();
  }
  // LLVM 3.3 Upgrade
  ArrayRef<Type *> parameterVector_array_ref(parameterVector);

  FunctionType *traceFuncType = FunctionType::get(
      Type::getVoidTy(*context), parameterVector_array_ref, true);
  Constant *traceFunc =
      module->getOrInsertFunction("printInstTracer", traceFuncType);

  ArrayRef<Value *> traceArgs_array_ref(values);

  CallInst::Create(traceFunc, traceArgs_array_ref, "", insertPoint);
}

AllocaInst *TraceVisitor::insertIntrinsicInstrumentation(
    Function *inst, Instruction *insertPoint, Instruction *alloca_insertPoint) {

  std::stringstream ss;
  ss << inst->getIntrinsicID();
  std::string str = ss.str();
  return insertStringInstrumentation(str, insertPoint, alloca_insertPoint);
}

void TraceVisitor::appendInitTyChar(std::stringstream& ss, Value* val) {
  if (Function *f = dyn_cast<Function>(val)) {
    if(f->getIntrinsicID()) {
      ss << "i";
    }
  } else if (BasicBlock* bb = dyn_cast<BasicBlock>(val)) {
    ss << "x";
  } else if(val->getType()->isPointerTy()){
    ss << "*";
  } else {
    ss << "v";
  }
}
void TraceVisitor::visitGeneric(Instruction &I) {
  if (!llfi::isLLFIIndexedInst(&I)) {
    return;
  }
  // errs() << "Dealing with " << I << "...\n";
  // errs() << "The number of operands:" << I.getNumOperands() << " -- \n";
  Instruction *insertPoint = getInsertPoint(&I);
  Instruction *alloca_insertPoint = getAllocaInsertPoint(&I);
  AllocaInst *aInst =
      insertInstrumentation(&I, I.getType(), insertPoint, alloca_insertPoint);
  std::vector<AllocaInst *> values;
  values.push_back(aInst);
  std::stringstream ss;
  appendInitTyChar(ss, &I);
  for (std::size_t i = 0; i != I.getNumOperands(); i++) {
    if (Function *f = dyn_cast<Function>(I.getOperand(i))) {
      if (f->getIntrinsicID()) {
        values.push_back(
            insertIntrinsicInstrumentation(f, insertPoint, alloca_insertPoint));
        ss << ":i";
      }
    }
    else if (BasicBlock* bb = dyn_cast<BasicBlock>(I.getOperand(i))) {
      errs() << llfi::getLLFIIndexofInst(&I) << "\n";
      values.push_back(insertBasicBlockInstrumentation(bb, insertPoint, alloca_insertPoint));
      ss << ":x";
    }

    else {
      values.push_back(insertInstrumentation(I.getOperand(i),
                                             I.getOperand(i)->getType(),
                                             insertPoint, alloca_insertPoint));
      if(I.getOperand(i)->getType()->isPointerTy()) {
        ss << ":*";
      } else {
        ss << ":v";
      }
    }
  }
  std::string str = ss.str();
  AllocaInst *typeString = insertStringInstrumentation(str, insertPoint, alloca_insertPoint);
  AllocaInst *opCodeInst = insertOpCode(&I, insertPoint, alloca_insertPoint);
  insertCall(&I, opCodeInst,typeString, values, insertPoint);
}

AllocaInst* TraceVisitor::insertBasicBlockInstrumentation(BasicBlock* inst, Instruction* insertPoint, Instruction* alloca_insertPoint) {
   std::stringstream ss;
  ss << llfi::getLLFIIndexofInst(inst->getFirstNonPHIOrDbgOrLifetime());
  std::string str = ss.str();
  //  std::string str("sdfsdf");
  const char *intrinsicNamePt = str.c_str();
  ArrayRef<uint8_t> intrinsic_name_array_ref((uint8_t *)intrinsicNamePt,
                                             str.size() + 1);
  // llvm::Value* OPCodeName = llvm::ConstantArray::get(context,
  // opcode_name_array_ref);
  llvm::Value *intrinsicName =
      llvm::ConstantDataArray::get(*context, intrinsic_name_array_ref);
  /********************************/

  errs() << *intrinsicName << "\n";
  AllocaInst *aInst = new AllocaInst(intrinsicName->getType(), "llfi_trace",
                                     alloca_insertPoint);
  new StoreInst(intrinsicName, aInst, insertPoint);
  return aInst;
}
void TraceVisitor::visitInstruction(Instruction &I) { visitGeneric(I); }

void TraceVisitor::visitBranchInst(BranchInst &BI) {
  visitGeneric(BI);
}
void TraceVisitor::visitCallInst(CallInst &CI) {
  if (!llfi::isLLFIIndexedInst(&CI)) {
    return;
  }

  visitGeneric(CI);
  //    CI.print(errs() << " -- \n");
  // only detects direct calls to pthread_create
  if (Function *calledFunc = CI.getCalledFunction()) {
    // shouldnt be done, names of values only for debugging. Take mangling
    // into consideration
    if (calledFunc->getName() == "pthread_create") {
      // errs() << "pthread_create function in: "<< CIentified\n";
      // get second argument of pthread_create (the target function)
      if (User *user = dyn_cast<User>(CI.getOperand(2))) {
        // isolate target of pthread_create
        if (Function *target = dyn_cast<Function>(
                user->stripPointerCasts())) { // getOperand(0))){

          // insert right at the beginning of the function
          Instruction *insertPoint =
              target->begin()->getFirstNonPHIOrDbgOrLifetime();

          // Allocate space on stack to pass the targetName at runtime
          const char *targetNamePt = target->getName().data();
          const std::string str(target->getName());
          ArrayRef<uint8_t> targetName_array_ref((uint8_t *)targetNamePt,
                                                 str.size() + 1);
          llvm::Value *targetName =
              llvm::ConstantDataArray::get(*context, targetName_array_ref);

          AllocaInst *targetNamePtr = new AllocaInst(
              targetName->getType(), "pthread-target", insertPoint);
          new StoreInst(targetName, targetNamePtr, insertPoint);

          // Create function signature
          std::vector<Type *> paramVector(1, targetNamePtr->getType());
          ArrayRef<Type *> paramVector_array_ref(paramVector);
          FunctionType *printTIDType = FunctionType::get(
              Type::getVoidTy(*context), paramVector_array_ref, false);
          Constant *printTIDFunc =
              module->getOrInsertFunction("printTID", printTIDType);

          // Prepare actual arguments which are passed
          std::vector<Value *> argVector(1, targetNamePtr);
          ArrayRef<Value *> argVector_array_ref(argVector);

          // Create and insert function call
          CallInst::Create(printTIDFunc, argVector_array_ref, "", insertPoint);
        }
      }
      //}
    }
  }
}

Instruction *TraceVisitor::getInsertPoint(Instruction *llfiIndexedInst) {
  Instruction *insertPoint;
  if (!llfiIndexedInst->isTerminator()) {
    insertPoint =
        llfi::getInsertPtrforRegsofInst(llfiIndexedInst, llfiIndexedInst);
    // if insert point is a call to inject fault, insert printInstTrace after
    // the injectFault call
    // iff injectFault occurs AFTER the targeted instruction (ie. dst targeted)
    insertPoint = llfi::changeInsertPtrIfInjectFaultInst(insertPoint);
  } else {
    // if terminator, insert before function
    insertPoint = llfiIndexedInst;
  }
  return insertPoint;
}

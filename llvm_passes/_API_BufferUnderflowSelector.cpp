#include "llvm/Pass.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/Instructions.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/ADT/Statistic.h"
#include "llvm/Support/CFG.h"
#include "llvm/ADT/DepthFirstIterator.h"
#include "llvm/ADT/GraphTraits.h"

#include "FIInstSelector.h"
#include "FICustomSelectorManager.h"
#include "_SoftwareFaultRegSelectors.h"

#include <fstream>
#include <iostream>
#include <set>
#include <string>
/**
 * This instruction selector only selects the API call functions as target
 */

using namespace llvm;
namespace llfi{
    class _API_BufferUnderflowInstSelector: public FIInstSelector{
        public:
            _API_BufferUnderflowInstSelector(){
                funcNames.insert(std::string("fread"));
                funcNames.insert(std::string("fwrite"));
            }

        private:
            std::set<std::string> funcNames;

        virtual bool isInstFITarget(Instruction* inst){
            if(isa<CallInst>(inst) == false)    return false;
            else{
                CallInst* CI = dyn_cast<CallInst>(inst);
                Function* called_func = CI->getCalledFunction();
                if(called_func == NULL) return false;
                std::string func_name = std::string(called_func->getName());
                if(funcNames.find(func_name) != funcNames.end())    return true;
                else return false;
            }
        } 
    };
    static RegisterFIInstSelector A( "BufferUnderflow(API)", new _API_BufferUnderflowInstSelector());
    static RegisterFIRegSelector B("BufferUnderflow(API)", new FuncArgRegSelector(2));
}

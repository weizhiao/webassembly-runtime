#include <llvm/Passes/StandardInstrumentations.h>
#include <llvm/Support/Error.h>
#include <llvm/ADT/SmallVector.h>
#include <llvm/ADT/Twine.h>
#include <llvm/ADT/Triple.h>
#include <llvm/Analysis/TargetTransformInfo.h>
#include <llvm/CodeGen/TargetPassConfig.h>
#include <llvm/ExecutionEngine/ExecutionEngine.h>
#include <llvm/MC/MCSubtargetInfo.h>
#include <llvm/Support/TargetSelect.h>
#include <llvm/Target/TargetMachine.h>
#include <llvm-c/Core.h>
#include <llvm-c/ExecutionEngine.h>
#include <llvm-c/Initialization.h>
#include <llvm/ExecutionEngine/GenericValue.h>
#include <llvm/ExecutionEngine/JITEventListener.h>
#include <llvm/ExecutionEngine/RTDyldMemoryManager.h>
#include <llvm/ExecutionEngine/Orc/LLJIT.h>
#include <llvm/IR/DerivedTypes.h>
#include <llvm/IR/Module.h>
#include <llvm/IR/Instructions.h>
#include <llvm/IR/IntrinsicInst.h>
#include <llvm/IR/LegacyPassManager.h>
#include <llvm/Support/CommandLine.h>
#include <llvm/Support/ErrorHandling.h>
#include <llvm/Target/CodeGenCWrappers.h>
#include <llvm/Target/TargetMachine.h>
#include <llvm/Target/TargetOptions.h>
#include <llvm/Transforms/Utils/LowerMemIntrinsics.h>
#include <llvm/Transforms/Vectorize/LoopVectorize.h>
#include <llvm/Transforms/Vectorize/LoadStoreVectorizer.h>
#include <llvm/Transforms/Vectorize/SLPVectorizer.h>
#include <llvm/Transforms/Scalar/LoopRotation.h>
#include <llvm/Transforms/Scalar/SimpleLoopUnswitch.h>
#include <llvm/Transforms/Scalar/LICM.h>
#include <llvm/Transforms/Scalar/GVN.h>
#include <llvm/Passes/PassBuilder.h>
#include <llvm/Analysis/TargetLibraryInfo.h>
#if LLVM_VERSION_MAJOR >= 12
#include <llvm/Analysis/AliasAnalysis.h>
#endif

#include <cstring>
#include "wasm_jit_llvm.h"

using namespace llvm;
using namespace llvm::orc;

LLVM_C_EXTERN_C_BEGIN

void wasm_jit_add_expand_memory_op_pass(LLVMPassManagerRef pass);

void wasm_jit_add_simple_loop_unswitch_pass(LLVMPassManagerRef pass);

void wasm_jit_apply_llvm_new_pass_manager(JITCompContext *comp_ctx, LLVMModuleRef module);

LLVM_C_EXTERN_C_END

ExitOnError ExitOnErr;

class ExpandMemoryOpPass : public llvm::ModulePass
{
public:
    static char ID;

    ExpandMemoryOpPass()
        : ModulePass(ID)
    {
    }

    bool runOnModule(Module &M) override;

    bool expandMemIntrinsicUses(Function &F);
    StringRef getPassName() const override
    {
        return "Expand memory operation intrinsics";
    }

    void getAnalysisUsage(AnalysisUsage &AU) const override
    {
        AU.addRequired<TargetTransformInfoWrapperPass>();
    }
};

char ExpandMemoryOpPass::ID = 0;

bool ExpandMemoryOpPass::expandMemIntrinsicUses(Function &F)
{
    Intrinsic::ID ID = F.getIntrinsicID();
    bool Changed = false;

    for (auto I = F.user_begin(), E = F.user_end(); I != E;)
    {
        Instruction *Inst = cast<Instruction>(*I);
        ++I;

        switch (ID)
        {
        case Intrinsic::memcpy:
        {
            auto *Memcpy = cast<MemCpyInst>(Inst);
            Function *ParentFunc = Memcpy->getParent()->getParent();
            const TargetTransformInfo &TTI =
                getAnalysis<TargetTransformInfoWrapperPass>().getTTI(
                    *ParentFunc);
            expandMemCpyAsLoop(Memcpy, TTI);
            Changed = true;
            Memcpy->eraseFromParent();
            break;
        }
        case Intrinsic::memmove:
        {
            auto *Memmove = cast<MemMoveInst>(Inst);
            expandMemMoveAsLoop(Memmove);
            Changed = true;
            Memmove->eraseFromParent();
            break;
        }
        case Intrinsic::memset:
        {
            auto *Memset = cast<MemSetInst>(Inst);
            expandMemSetAsLoop(Memset);
            Changed = true;
            Memset->eraseFromParent();
            break;
        }
        default:
            break;
        }
    }

    return Changed;
}

bool ExpandMemoryOpPass::runOnModule(Module &M)
{
    bool Changed = false;

    for (Function &F : M)
    {
        if (!F.isDeclaration())
            continue;

        switch (F.getIntrinsicID())
        {
        case Intrinsic::memcpy:
        case Intrinsic::memmove:
        case Intrinsic::memset:
            if (expandMemIntrinsicUses(F))
                Changed = true;
            break;

        default:
            break;
        }
    }

    return Changed;
}

void wasm_jit_add_expand_memory_op_pass(LLVMPassManagerRef pass)
{
    reinterpret_cast<legacy::PassManager *>(pass)->add(
        new ExpandMemoryOpPass());
}

void wasm_jit_add_simple_loop_unswitch_pass(LLVMPassManagerRef pass)
{
    reinterpret_cast<legacy::PassManager *>(pass)->add(
        createSimpleLoopUnswitchLegacyPass());
}

void wasm_jit_apply_llvm_new_pass_manager(JITCompContext *comp_ctx, LLVMModuleRef module)
{
    TargetMachine *TM =
        reinterpret_cast<TargetMachine *>(comp_ctx->target_machine);
    LLVMContext *CONTEXT = reinterpret_cast<LLVMContext *>(comp_ctx->context);
    PipelineTuningOptions PTO;
    PTO.LoopVectorization = true;
    PTO.SLPVectorization = true;
    PTO.LoopUnrolling = true;

    PassBuilder PB(TM, PTO);

    LoopAnalysisManager LAM;
    FunctionAnalysisManager FAM;
    CGSCCAnalysisManager CGAM;
    ModuleAnalysisManager MAM;

    std::unique_ptr<TargetLibraryInfoImpl> TLII(
        new TargetLibraryInfoImpl(Triple(TM->getTargetTriple())));
    FAM.registerPass([&]
                     { return TargetLibraryAnalysis(*TLII); });

    /* Register the AA manager first so that our version is the one used */
    AAManager AA = PB.buildDefaultAAPipeline();
    FAM.registerPass([&]
                     { return std::move(AA); });

    PB.registerFunctionAnalyses(FAM);
    PB.registerLoopAnalyses(LAM);
    PB.registerModuleAnalyses(MAM);
    PB.registerCGSCCAnalyses(CGAM);
    PB.crossRegisterProxies(LAM, FAM, CGAM, MAM);

    OptimizationLevel OL;

    switch (comp_ctx->opt_level)
    {
    case 0:
        OL = OptimizationLevel::O0;
        break;
    case 1:
        OL = OptimizationLevel::O1;
        break;
    case 2:
        OL = OptimizationLevel::O2;
        break;
    case 3:
    default:
        OL = OptimizationLevel::O3;
        break;
    }

    Module *M = reinterpret_cast<Module *>(module);

    ModulePassManager MPM;

    const char *Passes =
        "mem2reg,instcombine,simplifycfg,jump-threading,loop-vectorize,indvars";
    ExitOnErr(PB.parsePassPipeline(MPM, Passes));
    MPM.run(*M, MAM);
}

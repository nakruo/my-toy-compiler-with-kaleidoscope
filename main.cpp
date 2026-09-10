#include "KaleidoscopeJIT.h"
#include <iostream>
#include <algorithm>
#include <cassert>
#include <cctype>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <map>
#include <memory>
#include <string>
#include <vector>
#include "llvm/ADT/APFloat.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/IR/BasicBlock.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/DerivedTypes.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/PassManager.h"
#include "llvm/IR/Type.h"
#include "llvm/IR/Verifier.h"
#include "llvm/Passes/PassBuilder.h"
#include "llvm/Passes/StandardInstrumentations.h"
#include "llvm/Support/TargetSelect.h"
#include "llvm/Target/TargetMachine.h"
#include "llvm/Transforms/InstCombine/InstCombine.h"
#include "llvm/Transforms/Scalar.h"
#include "llvm/Transforms/Scalar/GVN.h"
#include "llvm/Transforms/Scalar/Reassociate.h"
#include "llvm/Transforms/Scalar/SimplifyCFG.h"
#include "llvm/Transforms/Utils/Mem2Reg.h"
#include "llvm/Support/TargetSelect.h"
#include "llvm/TargetParser/Host.h"
#include "llvm/MC/TargetRegistry.h"
#include "llvm/Target/TargetMachine.h"
#include "llvm/Support/FileSystem.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/IR/LegacyPassManager.h"

#ifdef _WIN32
#define DLLEXPORT __declspec(dllexport)
#else
#define DLLEXPORT
#endif

extern "C" DLLEXPORT double putchard(double X)
{
    fputc((char)X, stderr);
    return 0;
}



using namespace llvm;
using namespace llvm::orc;

enum Token 
{
    tok_eof = -1,

    tok_def = -2,
    tok_extern = -3,

    tok_identifier = -4,
    tok_number = -5,

    tok_if = -6,
    tok_then = -7,
    tok_else = -8,

    tok_for = -9,
    tok_in = -10,

    tok_unary = -11,
    tok_binary = -12,

    tok_var = -13,
};

static std::string IdentifierStr;
static double NumVal;

static int gettok()
{
    static int LastChar = ' ';

    while (isspace(LastChar)) LastChar = getchar();

    if (isalpha(LastChar)) //isalpha() checks if a character is an alphabetic letter. true: letter, false: other chr.
    {
        IdentifierStr = LastChar;
        while (isalnum((LastChar = getchar()))) IdentifierStr += LastChar;
        
        if (IdentifierStr == "def")    /**/
            return tok_def;
        if (IdentifierStr == "extern") /**/ 
            return tok_extern;
        
        if (IdentifierStr == "for") //
            return tok_for;
        if (IdentifierStr == "var")
            return tok_var;
        if (IdentifierStr == "in")
            return tok_in;

        if (IdentifierStr == "if") // 
            return tok_if;
        if (IdentifierStr == "then")
            return tok_then;
        if (IdentifierStr == "else")
            return tok_else;

        if (IdentifierStr == "binary") //
            return tok_binary;
        if (IdentifierStr == "unary")
            return tok_unary;
        
        return tok_identifier;
    
    }

    if (isdigit(LastChar) || LastChar == '.')
    {
        std::string NumStr;
        do {
            NumStr += LastChar;
            LastChar = getchar();
        } while (isdigit(LastChar) || LastChar == '.');
    
        NumVal = strtod(NumStr.c_str(), 0);
        return tok_number;
    }

    if (LastChar == '#')
    {
        do LastChar = getchar();
        while (LastChar != EOF && LastChar != '\n' && LastChar != '\r');

        if (LastChar != EOF) return gettok();
    }

    if (LastChar == EOF) return tok_eof;

    int ThisChar = LastChar;
    LastChar = getchar();
    return ThisChar;
}

class ExprAST 
{
public:
    virtual ~ExprAST() = default;
    virtual Value *codegen() = 0;
};

class NumberExprAST : public ExprAST 
{
    double Val;

public:
    NumberExprAST(double Val) : Val(Val) {}
    Value *codegen() override;
};

class VariableExprAST : public ExprAST 
{
    std::string Name;

public:
    VariableExprAST(const std::string &Name) : Name(Name) {}
    const std::string &getName() const {return Name;}
    Value *codegen() override;
};

class BinaryExprAST : public ExprAST
{
    char OP;
    std::unique_ptr<ExprAST> LHS, RHS;

public:
    BinaryExprAST(char OP, std::unique_ptr<ExprAST> LHS, std::unique_ptr<ExprAST> RHS)
        : OP(OP), LHS(std::move(LHS)), RHS(std::move(RHS)) {}
    Value *codegen() override;
};

class CallExprAST : public ExprAST
{
    std::string Callee;
    std::vector<std::unique_ptr<ExprAST>> Args;


public:
    CallExprAST(const std::string &Callee, std::vector<std::unique_ptr<ExprAST>> Args)
        :Callee(Callee), Args(std::move(Args)) {}
    Value *codegen() override;
};

class IFExprAST : public ExprAST 
{
    std::unique_ptr<ExprAST> Cond, Then, Else;

public:
    IFExprAST(std::unique_ptr<ExprAST> Cond, std::unique_ptr<ExprAST> Then, std::unique_ptr<ExprAST> Else)
        : Cond(std::move(Cond)), Then(std::move(Then)), Else(std::move(Else))
        {}
    
    Value *codegen() override;
};

class ForExprAST : public ExprAST 
{
    std::string VarName;
    std::unique_ptr<ExprAST> Start, End, Step, Body;

public:
    ForExprAST(const std::string &VarName, std::unique_ptr<ExprAST> Start, std::unique_ptr<ExprAST> End, std::unique_ptr<ExprAST> Step, std::unique_ptr<ExprAST> Body)
        : VarName(VarName), Start(std::move(Start)), End(std::move(End)), Step(std::move(Step)), Body(std::move(Body))
        {}
    
    Value *codegen() override;
};

class UnaryExprAST : public ExprAST 
{
    char Opcode;
    std::unique_ptr<ExprAST> Operand;

public:
    UnaryExprAST(char Opcode, std::unique_ptr<ExprAST> Operand)
        : Opcode(Opcode), Operand(std::move(Operand)) 
    {}

    Value *codegen() override;
};

class VarExprAST : public ExprAST
{
    std::vector<std::pair<std::string, std::unique_ptr<ExprAST>>> VarNames;
    std::unique_ptr<ExprAST> Body;

public:
    VarExprAST(std::vector<std::pair<std::string, std::unique_ptr<ExprAST>>> VarNames, std::unique_ptr<ExprAST> Body)
        : VarNames(std::move(VarNames)), Body(std::move(Body))
    {}

    Value *codegen() override;
};

class PrototypeAST
{
    std::string Name;
    std::vector<std::string> Args;
    bool IsOperator;
    unsigned Precedence;

public:
    PrototypeAST(const std::string &Name, std::vector<std::string> Args, bool IsOperator = false, unsigned Prec = 0)
        :Name(Name), Args(std::move(Args)), IsOperator(IsOperator), Precedence(Precedence)  {}

    
    const std::string &getName() const {return Name;}
    Function *codegen();

    bool isUnaryOp() const {return IsOperator && Args.size() == 1;}
    bool isBinaryOp() const {return IsOperator && Args.size() == 2;}

    char getOperatorName() const 
    {
        assert(isUnaryOp() || isBinaryOp());
        return Name[Name.size() - 1];
    }

    unsigned getBinaryPrecedence() const {return Precedence;}
};

class FunctionAST 
{
    std::unique_ptr<PrototypeAST> Proto;
    std::unique_ptr<ExprAST> Body;

public: 
    FunctionAST(std::unique_ptr<PrototypeAST> Proto, std::unique_ptr<ExprAST> Body)
        : Proto(std::move(Proto)), Body(std::move(Body)) {}
    Function *codegen();
};

static int CurTok;
static std::map<char, int> BinopPrecedence;

static std::unique_ptr<LLVMContext> TheContext;
static std::unique_ptr<IRBuilder<>> Builder;
static std::unique_ptr<Module> TheModule;
static std::map<std::string, AllocaInst*> NamedValues;
static std::map<std::string, std::unique_ptr<PrototypeAST>> FunctionProtos;
//optimization stuff 
static std::unique_ptr<FunctionPassManager> TheFPM;
static std::unique_ptr<LoopAnalysisManager> TheLAM;
static std::unique_ptr<FunctionAnalysisManager> TheFAM;
static std::unique_ptr<CGSCCAnalysisManager> TheCGAM;
static std::unique_ptr<ModuleAnalysisManager> TheMAM;
static std::unique_ptr<PassInstrumentationCallbacks> ThePIC;
static std::unique_ptr<StandardInstrumentations> TheSI;

static std::unique_ptr<KaleidoscopeJIT> TheJIT;
static ExitOnError ExitOnErr;
static void InitializeModuleAndManagers();

std::unique_ptr<ExprAST> LogError(const char *Str)
{
    fprintf(stderr, "Error: %s\n", Str);
    return nullptr;
}

std::unique_ptr<PrototypeAST> LogErrorP(const char *Str)
{
    LogError(Str);
    return nullptr;
}

Value *LogErrorV(const char *Str) {
  LogError(Str);
  return nullptr;
}

static AllocaInst *CreateEntryBlockAlloca(Function *TheFunction, StringRef VarName)
{
    IRBuilder<> TmpB(&TheFunction->getEntryBlock(), TheFunction->getEntryBlock().begin());
    return TmpB.CreateAlloca(Type::getDoubleTy(*TheContext), nullptr, VarName);
}

Function *getFunction(std::string Name) 
{
    if (auto *F = TheModule->getFunction(Name))
        return F;

    auto FI = FunctionProtos.find(Name);
    if (FI != FunctionProtos.end())
        return FI->second->codegen();
    
    return nullptr;
}

Value *NumberExprAST::codegen()
{
    return ConstantFP::get(*TheContext, APFloat(Val));
}

Value *VariableExprAST::codegen()
{
    AllocaInst *A = NamedValues[Name];
    if (!A) LogErrorV("Unknown variable name");
    return Builder->CreateLoad(A->getAllocatedType(), A, Name.c_str());
}

Value *BinaryExprAST::codegen()
{
    if (OP == '=')
    {
        VariableExprAST *LHSE = static_cast<VariableExprAST*>(LHS.get());
        if (!LHSE)
            return LogErrorV("destination of '=' must be a variable");
        
        Value *Val = RHS->codegen();
        if (!Val)   return nullptr;

        AllocaInst *Variable = NamedValues[LHSE->getName()];
        if (!Variable)
            return LogErrorV("Unknown variable name");
        Builder->CreateStore(Val, Variable);
        return Val;
    }
    
    Value *L = LHS->codegen();
    Value *R = RHS->codegen();
    if (!L || !R) return nullptr;

    switch (OP)
    {
        case '+':
            return Builder->CreateFAdd(L, R, "addtmp");
        case '-':
            return Builder->CreateFSub(L, R, "subtmp");
        case '*':
            return Builder->CreateFMul(L, R, "multmp");
        case '<':
            L = Builder->CreateFCmpULT(L, R, "cmptmp");
            return Builder->CreateUIToFP(L, Type::getDoubleTy(*TheContext), "booltmp");
        default:
            break;
    }

    Function *F = getFunction(std::string("binary") + OP);
    assert(F && "binary operator not found!");

    Value *Ops[2] = {L, R};
    return Builder->CreateCall(F, Ops, "binop");
}

Value *CallExprAST::codegen()
{
    Function *CalleeF = getFunction(Callee);
    if (!CalleeF)
        return LogErrorV("Unknown function referenced");
    if (CalleeF->arg_size() != Args.size())
        return LogErrorV("Incorrect # arguments passed");
    
    std::vector<Value *> ArgsV;
    for (unsigned i = 0, e = Args.size(); i != e; ++i)
    {
        ArgsV.push_back(Args[i]->codegen());
        if (!ArgsV.back())
            return nullptr;
    }

    return Builder->CreateCall(CalleeF, ArgsV, "calltmp");
}

Function *PrototypeAST::codegen()
{
    std::vector<Type*> Doubles(Args.size(), Type::getDoubleTy(*TheContext));

    FunctionType *FT = FunctionType::get(Type::getDoubleTy(*TheContext), Doubles, false);
    Function *F = Function::Create(FT, Function::ExternalLinkage, Name, TheModule.get());

    unsigned Idx = 0;
    for (auto &Arg : F->args())
        Arg.setName(Args[Idx++]);
    return F;

}

Function *FunctionAST::codegen()
{
    auto &P = *Proto;
    FunctionProtos[Proto->getName()] = std::move(Proto);
    Function *TheFunction = getFunction(P.getName());
    
    if (!TheFunction)
        return nullptr;
    
    if (P.isBinaryOp())
        BinopPrecedence[P.getOperatorName()] = P.getBinaryPrecedence();

    if (!TheFunction->empty())
        return (Function*)LogErrorV("Function cannot be redefined.");

    BasicBlock *BB = BasicBlock::Create(*TheContext, "entry", TheFunction);
    Builder->SetInsertPoint(BB);

    NamedValues.clear();
    for (auto &Arg : TheFunction->args())
    {
        AllocaInst *Alloca = CreateEntryBlockAlloca(TheFunction, Arg.getName());
        Builder->CreateStore(&Arg, Alloca);
        NamedValues[std::string(Arg.getName())] = Alloca;
    }

    if (Value *RetVal = Body->codegen())
    {
        Builder->CreateRet(RetVal);
        verifyFunction(*TheFunction);
        TheFPM->run(*TheFunction, *TheFAM);
        return TheFunction;
    }

    TheFunction->eraseFromParent();
    return nullptr;
}

Value *IFExprAST::codegen()
{
    Value *CondV = Cond->codegen();
    if (!CondV)
        return nullptr;

    CondV = Builder->CreateFCmpONE(CondV, ConstantFP::get(*TheContext, APFloat(0.0)), "ifcond" );
    Function *TheFunction = Builder->GetInsertBlock()->getParent();

    BasicBlock *ThenBB = BasicBlock::Create(*TheContext, "then", TheFunction);
    BasicBlock *ElseBB = BasicBlock::Create(*TheContext, "else");
    BasicBlock *MergeBB = BasicBlock::Create(*TheContext, "ifcont");

    Builder->CreateCondBr(CondV, ThenBB, ElseBB);
    
    Builder->SetInsertPoint(ThenBB);
    Value *ThenV = Then->codegen();
    if (!ThenV)
        return nullptr;
    Builder->CreateBr(MergeBB);
    ThenBB = Builder->GetInsertBlock();

    TheFunction->insert(TheFunction->end(), ElseBB);
    Builder->SetInsertPoint(ElseBB);
    Value *ElseV = Else->codegen();
    if (!ElseV)
        return nullptr;
    Builder->CreateBr(MergeBB);
    ElseBB = Builder->GetInsertBlock();

    TheFunction->insert(TheFunction->end(), MergeBB);
    Builder->SetInsertPoint(MergeBB);

    PHINode *PN = Builder->CreatePHI( Type::getDoubleTy(*TheContext), 2, "iftmp" );

    PN->addIncoming(ThenV, ThenBB);
    PN->addIncoming(ElseV, ElseBB);
    return PN;

}

Value *ForExprAST::codegen()
{
    Value *StartVal = Start->codegen();
    if (!StartVal)  return nullptr;

    Function *TheFunction = Builder->GetInsertBlock()->getParent();
    AllocaInst *Alloca = CreateEntryBlockAlloca(TheFunction, VarName);
    Builder->CreateStore(StartVal, Alloca);

    BasicBlock *LoopBB = BasicBlock::Create(*TheContext, "loop", TheFunction);
    Builder->CreateBr(LoopBB);
    Builder->SetInsertPoint(LoopBB);

    AllocaInst *OldVal = NamedValues[VarName];
    NamedValues[VarName] = Alloca;

    if (!Body->codegen())
        return nullptr;

    Value *StepVal = nullptr;
    if (Step)
    {
        StepVal = Step->codegen();
        if (!StepVal)
            return nullptr;
        
    }
    else
    {
        StepVal = ConstantFP::get(*TheContext, APFloat(1.0));
    }

    Value *CurVar = Builder->CreateLoad(Alloca->getAllocatedType(), Alloca, VarName.c_str());
    Value *NextVar = Builder->CreateFAdd(CurVar, StepVal, "nextvar");
    Builder->CreateStore(NextVar, Alloca);

    Value *EndCond = End->codegen();
    if (!EndCond)
        return nullptr;
        
    EndCond = Builder->CreateFCmpONE(EndCond, ConstantFP::get(*TheContext, APFloat(0.0)), "loopcond");

    BasicBlock *LoopEndBB = Builder->GetInsertBlock();
    BasicBlock *AfterBB = BasicBlock::Create(*TheContext, "afterloop", TheFunction);
    Builder->CreateCondBr(EndCond, LoopBB, AfterBB);
    Builder->SetInsertPoint(AfterBB);

    if (OldVal)
        NamedValues[VarName] = OldVal;
    else 
        NamedValues.erase(VarName);
    
        return Constant::getNullValue(Type::getDoubleTy(*TheContext));
}

Value *UnaryExprAST::codegen() 
{
    Value *OperandV = Operand->codegen();
    if (!OperandV)
        return nullptr;

    Function *F = getFunction(std::string("unary") + Opcode);
    if (!F)
        return LogErrorV("Unknown unary operator");
    
    return Builder->CreateCall(F, OperandV, "unop");
}

Value *VarExprAST::codegen()
{
    std::vector<AllocaInst *> OldBindings;
    Function *TheFunction = Builder->GetInsertBlock()->getParent();

    for (unsigned i = 0, e = VarNames.size(); i != e; ++i)
    {
        const std::string &VarName = VarNames[i].first;
        ExprAST *Init = VarNames[i].second.get();

        Value *InitVal;
        if (Init)
        {
            InitVal = Init->codegen();
            if (!InitVal)
                return nullptr;
        } else
        {
            InitVal = ConstantFP::get(*TheContext, APFloat(0.0));
        }
        AllocaInst *Alloca = CreateEntryBlockAlloca(TheFunction, VarName);
        Builder->CreateStore(InitVal, Alloca);
        OldBindings.push_back(NamedValues[VarName]);
        
        NamedValues[VarName] = Alloca;
    }

    Value *BodyVal = Body->codegen();
    if (!BodyVal)
        return nullptr;
    
    for (unsigned i = 0, e = VarNames.size(); i != e; ++i)
        NamedValues[VarNames[i].first] = OldBindings[i];
    
    return BodyVal;
}

static int getNextTOken() 
{
    return CurTok = gettok();
}

static std::unique_ptr<ExprAST> ParseNumberEXpr()
{
    auto Result = std::make_unique<NumberExprAST>(NumVal);
    getNextTOken();
    return std::move(Result);
}


static std::unique_ptr<ExprAST> ParseExpression();
static std::unique_ptr<ExprAST> ParseParenExpr() 
{
    getNextTOken();
    auto V = ParseExpression();
    if (!V) 
        nullptr;
    
    if (CurTok != ')')
        return LogError("expected ')'");
    getNextTOken();
    
    return V;
}

static std::unique_ptr<ExprAST> ParseIdentifierExpr()
{
    std::string IdName = IdentifierStr;
    getNextTOken();

    if (CurTok != '(') return std::make_unique<VariableExprAST>(IdName);

    getNextTOken(); 
    std::vector<std::unique_ptr<ExprAST>> Args;
    if (CurTok != ')' )
    {
        while(true) 
        {
            if (auto Arg = ParseExpression())
                Args.push_back(std::move(Arg));
            else    
                return nullptr;
            
            if (CurTok == ')') break;
            
            if (CurTok != ',')
                return LogError("Expected ')' or ',' in argument list");
            getNextTOken();

        }
    }
    getNextTOken();
    return std::make_unique<CallExprAST>(IdName, std::move(Args));
}

static std::unique_ptr<ExprAST> ParseIFExpr()
{
    getNextTOken();

    auto Cond = ParseExpression();
    if (!Cond)
        return nullptr;
    
    if (CurTok != tok_then)
        return LogError("expected then");
    getNextTOken();

    auto Then = ParseExpression();
    if (!Then)
        return nullptr;
    
    if (CurTok != tok_else)
        return LogError("expected else");
    getNextTOken();

    auto Else = ParseExpression();
    if (!Else)
        return nullptr;

    return std::make_unique<IFExprAST>(std::move(Cond), std::move(Then), std::move(Else));

}

static std::unique_ptr<ExprAST> ParseForExpr()
{
    getNextTOken();

    if (CurTok != tok_identifier)
        return LogError("expected identifier after for");

    std::string IdName = IdentifierStr;
    getNextTOken();

    if (CurTok != '=')
        return LogError("expected '=' after for");
    getNextTOken();

    auto Start = ParseExpression();
    if (!Start)
        return nullptr;
    if (CurTok != ',')
        return LogError("expected ',' after for start value");
    getNextTOken();
    
    auto End = ParseExpression();
    if (!End) return nullptr;

    std::unique_ptr<ExprAST> Step;
    if (CurTok == ',')
    {
        getNextTOken();
        Step = ParseExpression();
        if (!Step)
            return nullptr;
    }

    if (CurTok != tok_in)
        return LogError("expected 'in' after for");
    getNextTOken();

    auto Body = ParseExpression();
    if (!Body)
        return nullptr;

    return std::make_unique<ForExprAST>(IdName, std::move(Start), std::move(End), std::move(Step), std::move(Body));
}

static std::unique_ptr<ExprAST> ParseVarExpr()
{
    getNextTOken();
    std::vector<std::pair<std::string, std::unique_ptr<ExprAST>>> VarNames;

    if (CurTok != tok_identifier)
        return LogError("expected identifier after var");

    while (true)
    {
        std::string Name = IdentifierStr;
        getNextTOken();

        std::unique_ptr<ExprAST> Init;
        if (CurTok == '=')
        {
            getNextTOken();
            Init = ParseExpression();
            if (!Init) return nullptr;
        }

        VarNames.push_back(std::make_pair(Name, std::move(Init)));

        if (CurTok != ',') break;
        getNextTOken();

        if (CurTok != tok_identifier)
            return LogError("expected identifier list after var");

    }
    
    if (CurTok != tok_in)
        return LogError("expected 'in' keyword after 'var'");
    getNextTOken();

    auto Body = ParseExpression();
    if (!Body)
        return nullptr;
        
    return std::make_unique<VarExprAST>(std::move(VarNames), std::move(Body));
}

static std::unique_ptr<ExprAST> ParsePrimary() 
{
    switch (CurTok)
    {
    default:
        return LogError("Unknown token when expecting an expression");
    case tok_if:
        return ParseIFExpr();
    case tok_for:
        return ParseForExpr();
    case tok_var:
        return ParseVarExpr();
    case tok_identifier:
        return ParseIdentifierExpr();
    case tok_number:
        return ParseNumberEXpr();
    case '(':
        return ParseParenExpr();
    }
}

static std::unique_ptr<ExprAST> ParseUnary()
{
    if (!isascii(CurTok) || CurTok == '(' || CurTok == ',')
        return ParsePrimary();

    int Opc = CurTok;
    getNextTOken();
    if (auto Operand = ParseUnary())
        return std::make_unique<UnaryExprAST>(Opc, std::move(Operand));
    return nullptr;
}

static int GetTokPrecedence() 
{
    if (!isascii(CurTok))
        return -1;

    int TokPrec = BinopPrecedence[CurTok];
    
    if (TokPrec <= 0) 
        return -1;
    return TokPrec;
}

static std::unique_ptr<ExprAST> ParseBinOpRHS(int ExprPrec, std::unique_ptr<ExprAST> LHS)
{
    while (true) 
    {
        int TokPrec = GetTokPrecedence();

        if(TokPrec < ExprPrec)
            return LHS;
        
        int BinOp = CurTok;
        getNextTOken();

        auto RHS = ParseUnary();
        if (!RHS) return nullptr;

        int NextPrec = GetTokPrecedence();
        if (TokPrec < NextPrec)
        {
            RHS = ParseBinOpRHS(TokPrec + 1, std::move(RHS));
            if (!RHS) return nullptr;
        }


        LHS = std::make_unique<BinaryExprAST>(BinOp, std::move(LHS), std::move(RHS));
    }

    
}

static std::unique_ptr<ExprAST> ParseExpression() 
{
    auto LHS = ParseUnary();
    if (!LHS) return nullptr;
    return ParseBinOpRHS(0, std::move(LHS));
}

static std::unique_ptr<PrototypeAST> ParsePrototype() 
{
    std::string FnName;

    unsigned Kind = 0; 
    unsigned BinaryPrecedence = 30;

    switch (CurTok) 
    {
    default:
        return LogErrorP("Expected function name in prototype");
    case tok_identifier:
        FnName = IdentifierStr;
        Kind = 0;
        getNextTOken();
        break;
    case tok_unary:
        getNextTOken();
        if (!isascii(CurTok))
            return  LogErrorP("Expected unary operator");
        FnName = "unary";
        FnName += (char)CurTok;
        Kind = 1;
        getNextTOken();
        break;
    case tok_binary:
        getNextTOken();
        if (!isascii(CurTok))
            return LogErrorP("Expected binary operator");
        FnName = "binary";
        FnName += (char)CurTok;
        Kind = 2;
        getNextTOken();

        if (CurTok == tok_number) {
            if (NumVal < 1 || NumVal > 100)
                return LogErrorP("Invalid precedence: must be 1..100");
            BinaryPrecedence = (unsigned)NumVal;
            getNextTOken();
        }
        break;
    }

    if (CurTok != '(')
        return LogErrorP("Expected '(' in prototype");

    std::vector<std::string> ArgNames;
    while (getNextTOken() == tok_identifier)
        ArgNames.push_back(IdentifierStr);
    if (CurTok != ')')
        return LogErrorP("Expected ')' in prototype");

    getNextTOken(); 

    if (Kind && ArgNames.size() != Kind)
        return LogErrorP("Invalid number of operands for operator");

    return std::make_unique<PrototypeAST>(FnName, std::move(ArgNames), Kind != 0, BinaryPrecedence);
}

static std::unique_ptr<FunctionAST> ParseDefinition()
{
    getNextTOken();
    auto Proto = ParsePrototype();

    if (!Proto) return nullptr;
    
    if (auto e = ParseExpression())
        return std::make_unique<FunctionAST>(std::move(Proto), std::move(e));
    return nullptr;
}

static std::unique_ptr<PrototypeAST> ParseExtern()
{
    getNextTOken();
    return ParsePrototype();
}

static std::unique_ptr<FunctionAST> ParseTopLevelExpr() 
{
    if (auto e = ParseExpression())
    {
        auto Proto = std::make_unique<PrototypeAST>("__anon_expr", std::vector<std::string>());
        return std::make_unique<FunctionAST>(std::move(Proto), std::move(e));
    }
    return nullptr;
}

static void HandleDefinition()
{
    if (auto FnAST = ParseDefinition())
    {
        if (auto *FnIR = FnAST->codegen())
        {
            fprintf(stderr, "Read function definition;\n");
            FnIR->print(errs());
            fprintf(stderr, "\n");

        }
    }
    else 
    {
        getNextTOken();
    }

}
static void HandleExtern()
{
    if (auto ProtoAST = ParseExtern())
    {
        if (auto *FnIR = ProtoAST->codegen())
        {
            fprintf(stderr, "Read extern: \n");
            FnIR->print(errs());
            fprintf(stderr, "\n");

            FunctionProtos[ProtoAST->getName()] = std::move(ProtoAST);
        }
    }
    else
    {
        getNextTOken();
    }
}
static void HandleTopLevelExpression()
{
    if (auto FnAST = ParseTopLevelExpr()) 
    {
      if (FnAST->codegen()) 
      {
        fprintf(stderr, "Parsed top-level expr\n");
      }
    }
    else
    {
        getNextTOken();
    }
}

static void InitializeModuleAndManagers()
{
    TheContext = std::make_unique<LLVMContext>();
    TheModule = std::make_unique<Module>("my first JIT Module", *TheContext);
    Builder = std::make_unique<IRBuilder<>>(*TheContext);

    //TheModule->setDataLayout(TheJIT->getDataLayout()); 
    TheFPM = std::make_unique<FunctionPassManager>();
    TheLAM = std::make_unique<LoopAnalysisManager>();
    TheFAM = std::make_unique<FunctionAnalysisManager>();
    TheCGAM = std::make_unique<CGSCCAnalysisManager>();
    TheMAM = std::make_unique<ModuleAnalysisManager>();
    ThePIC = std::make_unique<PassInstrumentationCallbacks>();
    TheSI = std::make_unique<StandardInstrumentations>(*TheContext, true);
    TheSI->registerCallbacks(*ThePIC, TheMAM.get());
    
    TheFPM->addPass(PromotePass());
    TheFPM->addPass(InstCombinePass());
    TheFPM->addPass(ReassociatePass());
    TheFPM->addPass(GVNPass());
    TheFPM->addPass(SimplifyCFGPass());

    PassBuilder PB;
    PB.registerModuleAnalyses(*TheMAM);
    PB.registerFunctionAnalyses(*TheFAM);
    PB.crossRegisterProxies(*TheLAM, *TheFAM, *TheCGAM, *TheMAM);
}

static void MainLoop()
{
    while (true) 
    {
        fprintf(stderr, "ready> ");
        switch (CurTok)
        {
            case tok_eof:
                return;
            case ';':
                getNextTOken();
                break;
            case tok_def:
                HandleDefinition();
                break;
            case tok_extern:
                HandleExtern();
                break;
            default:
                HandleTopLevelExpression();
                break;   
        }
    }
}




int main()
{
        
    BinopPrecedence['<'] = 10;
    BinopPrecedence['+'] = 20;
    BinopPrecedence['-'] = 20;
    BinopPrecedence['*'] = 40;
    BinopPrecedence['/'] = 40;  
    BinopPrecedence['%'] = 40; 
    BinopPrecedence['='] = 2;

    InitializeNativeTarget();
    InitializeNativeTargetAsmParser();
    InitializeNativeTargetAsmPrinter();

    auto TargetTriple = sys::getDefaultTargetTriple();

    std::string Error;
    auto Target = TargetRegistry::lookupTarget(TargetTriple, Error);

    if (!Target)
    {
        errs() << Error;
        return 1;
    }

    auto CPU = "generic";
    auto Features = "";

    TargetOptions opt;
    auto TargetMachine = Target->createTargetMachine(TargetTriple, CPU, Features, opt, Reloc::PIC_);

    InitializeModuleAndManagers();

    TheModule->setDataLayout(TargetMachine->createDataLayout());
    TheModule->setTargetTriple(TargetTriple);

    fprintf(stderr, "ready> ");
    getNextTOken();
    MainLoop();

    auto filename = "output.o";
    std::error_code EC;
    raw_fd_ostream dest(filename, EC, sys::fs::OF_None);

    if (EC)
    {
        errs() << "Could not open file: " << EC.message();
        return 1;
    }
    legacy::PassManager pass;
    auto fileType = CodeGenFileType::ObjectFile;

    if (TargetMachine->addPassesToEmitFile(pass, dest, nullptr, fileType))
    {
        errs() << "TargetMachine can't emit a file of this type";
        return 1;
    }

    pass.run(*TheModule);
    dest.flush();

    return 0;
}
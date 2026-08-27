#include <iostream>
#include <memory>
#include <vector>
#include <map>
#include "llvm/ADT/APFloat.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/IR/BasicBlock.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/DerivedTypes.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/Type.h"
#include "llvm/IR/Verifier.h"

using namespace llvm;

enum Token 
{
    tok_eof = -1,

    tok_def = -2,
    tok_extern = -3,

    tok_identifier = -4,
    tok_number = -5,
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
        if (IdentifierStr == "def") 
            return tok_def;
        if (IdentifierStr == "extern")
            return tok_extern;
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

class PrototypeAST
{
    std::string Name;
    std::vector<std::string> Args;

public:
    PrototypeAST(const std::string &Name, std::vector<std::string> Args)
        :Name(Name), Args(std::move(Args)) {}

    const std::string &getName() const { return Name; }

};

class FunctionAST 
{
    std::unique_ptr<PrototypeAST> Proto;
    std::unique_ptr<ExprAST> Body;

public: 
    FunctionAST(std::unique_ptr<PrototypeAST> Proto, std::unique_ptr<ExprAST> Body)
        : Proto(std::move(Proto)), Body(std::move(Body)) {}
};

static int CurTok;

static std::unique_ptr<LLVMContext> TheContext;
static std::unique_ptr<IRBuilder<>> Builder;
static std::unique_ptr<Module> TheModule;
static std::map<std::string, Value *> NamedValues;

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

Value *NumberExprAST::codegen()
{
    return ConstantFP::get(*TheContext, APFloat(Val));
}

Value *VariableExprAST::codegen()
{
    Value *V = NamedValues[Name];
    if (!V) LogErrorV("Unknown variable name");
    return V;
}

Value *BinaryExprAST::codegen()
{
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
            return LogErrorV("invalid binary operator");
    }
}

Value *CallExprAST::codegen()
{
    Function *CalleeF = TheModule->getFunction(Callee);
    if (!CalleeF)
        return LogErrorV("Unknown funcition referenced");
    if (CalleeF->arg_size() != Args.size())
        return LogErrorV("Incorrect # arguments passed");
    
    std::vector<Value *> ArgsV;
    for (unsigned i = 0, e = Args.size(); i != e; ++i)
    {
        ArgsV.push_back(Args[i]->codegen());
        if (ArgsV.back())
            return nullptr;
    }

    return Builder->CreateCall(CalleeF, ArgsV, "calltmp");
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
        return LogError("excepted ')'");
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

static std::unique_ptr<ExprAST> ParsePrimary() 
{
    switch (CurTok)
    {
    default:
        return LogError("Unknown token when expecting an expression");
    case tok_identifier:
        return ParseIdentifierExpr();
    case tok_number:
        return ParseNumberEXpr();
    case '(':
        return ParseParenExpr();
    }
}

static std::map<char, int> BinopPrecedence;

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

        auto RHS = ParsePrimary();
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
    auto LHS = ParsePrimary();
    if (!LHS) return nullptr;
    return ParseBinOpRHS(0, std::move(LHS));
}

static std::unique_ptr<PrototypeAST> ParsePrototype()
{
    if (CurTok != tok_identifier)
        return LogErrorP("Expected function name in prototype");

    std::string FnName = IdentifierStr;
    getNextTOken();

    if (CurTok != '(')
        return LogErrorP("Expected '(' in prototoype");
    
    std::vector<std::string> ArgNames;
    while (getNextTOken() == tok_identifier) 
        ArgNames.push_back(IdentifierStr);
        
    if ( CurTok != ')')
        return LogErrorP("Expected ')' in prototoype");
    
    getNextTOken();
    return std::make_unique<PrototypeAST>(FnName, std::move(ArgNames));
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
    if (ParseDefinition())
    {
        fprintf(stderr, "Parsed a function definition.\n");
    } 
    else getNextTOken();

}
static void HandleExtern()
{
    if (ParseExtern())
    {
        fprintf(stderr, "Parsed and extern\n");
    }
    else getNextTOken();
}
static void HandleTopLevelExpression()
{
    if (ParseTopLevelExpr())
    {
        fprintf(stderr, "Parsed a toplevel exp\n");
    }
    else getNextTOken();
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
    BinopPrecedence['/'] = 40; // this 
    BinopPrecedence['%'] = 40; // and that. i added them myself

    fprintf(stderr, "ready> ");
    getNextTOken();

    MainLoop();

    return 0;
}
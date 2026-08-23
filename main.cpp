#include <iostream>
#include <memory>
#include <vector>


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
};

class NumberExprAST : public ExprAST 
{
    double Val;

public:
    NumberExprAST(double Val) : Val(Val) {}
};

class VariableExprAST : public ExprAST 
{
    std::string Name;

public:
    VariableExprAST(const std::string &Name) : Name(Name) {}
};

class BinaryExprAST : public ExprAST
{
    char OP;
    std::unique_ptr<ExprAST> LHS, RHS;

public:
    BinaryExprAST(char OP, std::unique_ptr<ExprAST> LHS, std::unique_ptr<ExprAST> RHS)
        : OP(OP), LHS(std::move(LHS)), RHS(std::move(RHS)) {}
};

class CallExprAST : public ExprAST
{
    std::string Callee;
    std::vector<std::unique_ptr<ExprAST>> Args;


public:
    CallExprAST(const std::string &Callee, std::vector<std::unique_ptr<ExprAST>> Args)
        :Callee(Callee), Args(std::move(Args)) {}
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
static int getNextTOken() 
{
    return CurTok = gettok();
}

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

int main()
{
    return 0;
}